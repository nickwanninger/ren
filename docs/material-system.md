# Material System Design

Status: design notes, not yet implemented. Brainstormed 2026-09-06.

Goal: Unity-style material ergonomics — the **shader defines the parameters**, and a
material instance is just a reference to a shader plus values for those parameters
(which texture is albedo, what is the base color, is there a default emissive).

## Where we're starting from

The hard part is already done. `ShaderReflection::MaterialSchema`
(`src/ren/renderer/shader/ShaderReflection.h`) gives us, per shader: field names,
types, byte offsets/sizes, alignment, and **attributes** (`[Source]`, `[Color]`,
`[FloatRange]`, `[DisplayName]`, `[Default]`).

That is exactly Unity's `Properties {}` block, except we didn't have to invent a
second language for it — the Slang struct *is* the schema.

`ren/core.slang` already defines the ABI:

```slang
[MaterialDrawParams]
public struct MaterialInput<MaterialT> {
  ObjectData *object;
  MaterialT  *material;
}
```

Two device pointers in a push constant. **There is no descriptor binding to solve,
only "where do those two pointers point."**

So the whole material system reduces to:

1. `MaterialTemplate` — shader + schema (one per `.slang`, cached)
2. `Material` — template ref + a `std::vector<u8>` blob shaped by the schema + a
   side table of `ref<Texture>` keeping textures alive
3. Per draw: get a GPU address for that blob, write the push constant, draw

Point 3 was the open question.

## Decisions

### Material params are settable at high frequency

Confirmed requirement: gameplay/script code can write params any frame (color
pulses, dissolve animations), like Unity.

This makes the **per-frame copy-everything arena** the *correct* answer, not a
compromise. If anything can change any frame, dirty-tracking buys nothing — worst
case you re-upload everything anyway, having paid for a free list, triple-buffered
dirty flags, and staging copies to get there.

- No dirty tracking. Reset at frame start, memcpy every visible material, done.
- No N-frames-in-flight hazard. Frame N's arena region is written by frame N and
  read by frame N — nothing aliases.
- Setters are trivial: `set()` writes the CPU blob. No invalidation, no "which
  frame's copy is stale."

### `Material` is concrete and `final`

The existing `ren::Material` / `ren::PBRMaterial` pair is **dead code** — unused,
and `PBRMaterial::bind()` is a `// TODO`. There is nothing to migrate. Delete both.

An abstract base with `virtual bind()` actively conflicts with the goal: Unity-style
means there is exactly *one* C++ material class, and the shader is the variation.
`PBRMaterial` with its hardcoded `PBRMaterialProperties` struct is the old model;
keeping both means the schema lives in two places that can silently disagree.

```cpp
class Material final : public MaterialAsset {
  ref<MaterialTemplate> tmpl;      // shader + schema + PSO
  std::vector<u8>       blob;      // sized/laid out by schema
  std::vector<TexSlot>  textures;  // {byteOffset, ref<Texture>}
  MaterialHandle        handle;
};
```

No `virtual bind()`. Binding is a free function or a method on the pass — the
*renderer* knows the pass, the material is just data:

```cpp
enc.bindGraphics(mat.tmpl->pso());
enc.pushConstants(MaterialInput{objectAddr, heap.address(mat.handle)});
```

`PipelineStateObject` therefore lives on the `MaterialTemplate` — one PSO per
shader, shared across instances. That's what the current `static PBRMaterial::pso`
was groping toward.

`isDeferred()` becomes `tmpl->renderQueue()`, read from a shader attribute — along
with cull mode and depth state. Those are Unity's `Tags`/`ZWrite` and belong next
to the shader, not in a C++ subclass.

`pbr.slang` already declares `struct PBRMaterial { ... }`. Let that be the *only*
definition of what a PBR material is.

### Stable handles from day one

Even though the *address* is per-frame, identity must be stable. Not for
GPU-driven-later hedging — for a concrete reason now: a material visible in
gbuffer + shadow + picking must upload **once per frame, not three times**. The
`uploadedFrame` memo needs identity.

Slot = `u32` index into a `vector<Material*>`; per-frame you store
`{arenaOffset, uploadedFrame}` beside it.

## V1: `MaterialHeap` (copy everything, per-frame arena)

```cpp
VkDeviceAddress MaterialHeap::address(MaterialHandle h) {
  auto &e = slots[h.index];
  if (e.uploadedFrame != currentFrame) {
    e.offset = arena.allocate(bytes);
    resolveTextureHandles(mat);   // re-pack u32s from ref<Texture>
    memcpy(arena.host() + e.offset, mat.blob.data(), bytes);
    e.uploadedFrame = currentFrame;
  }
  return arena.devicePointer(e.offset);
}
```

- One host-visible arena, **triple-buffered** by `getFrameIndex()`, `reset(0)` each
  frame. `ArenaBuffer` (`src/ren/renderer/Buffer.h`) is a single allocation today,
  so triple-buffering it like `UniformBufferSet` is the one piece of plumbing to add
  — otherwise frame N's reset stomps frame N-2's in-flight reads.
- `slots[handle] = {arenaOffset, uploadedFrame}`.

### The resize trap

`ArenaBuffer::allocate` reallocates on overflow, which **invalidates every device
address already handed out this frame**.

Two fixes, in order of effort:

1. *V1:* reserve a generous fixed capacity up front and assert on overflow. You'll
   see the assert long before you see a corrupted draw.
2. *Better:* two-phase frame — walk the visible set, sum sizes, `ensure()` once,
   then hand out addresses. `RenderWorld::extractFromECS` already produces the
   renderable list, so phase one is nearly free.

## V2 (only if profiling says so): staging + device-local heap

The one real cost V1 retains is host-visible reads. Materials are read by every
fragment; that's the access pattern that actually cares. If it shows up in a
profile, the fix is structurally cheap because the two-phase frame already exists:

- Write the CPU blobs into a **host-visible staging arena** (triple-buffered, write-
  combined: `VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT` — pure
  sequential memcpy, never read back).
- One `vkCmdCopyBuffer` at frame start into a **single, non-triple-buffered
  device-local heap**. It needn't be triple-buffered: the GPU only reads it after
  the copy in the same frame's command stream, and the copy is ordered against the
  previous frame's reads by the frame fence.
- Grow the device heap to the high-water mark, never shrink (or shrink lazily after
  N quiet frames). Phase one gives the exact total before any address is handed out,
  so `ensure(total)` there is safe — no addresses exist yet at that moment.

Wins: one copy + one barrier at full DMA bandwidth; device-local reads; WC staging.

### Barrier

```
srcStage  = VK_PIPELINE_STAGE_2_COPY_BIT           srcAccess = TRANSFER_WRITE
dstStage  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VERTEX_SHADER_BIT
dstAccess = VK_ACCESS_2_SHADER_STORAGE_READ_BIT
```

Be precise: dereferencing a `MaterialT*` is a buffer-device-address load, which is
a **storage** read, not a uniform read.

This should be a declared render-graph edge rather than a hand-rolled barrier, so
the graph can batch it with the other frame-start uploads (`engineUBOBuffer`, object
data, debug lines). **Open:** check whether `RenderGraph` models buffers as
resources at all — if it only tracks images, that's a small gap to fill, and this is
a good forcing function.

## V3 (further out): dirty-set copy

Once the device heap is persistent, a material that *didn't* change can keep its
slot and be skipped in the copy. The frame's copy list goes from "everything
visible" to "everything visible that's dirty" — just a list of `VkBufferCopy`
regions in the same single `vkCmdCopyBuffer`. Same one command, fewer bytes.

Cost: requires stable slots *across* frames, which brings back the free list and the
N-frames-in-flight write hazard (a material dirtied on frame N can't overwrite bytes
frame N-1 might still be reading).

**Don't build this yet.** The upgrade is additive and touches neither the ABI, the
schema layer, nor the shaders.

A likely-better variant: `[Static]`-tagged templates go to the device-local heap,
everything else stays in the per-frame arena. Also additive.

## Sharp edges

1. **Layout mismatch.** The blob is CPU-side; the GPU reads it via a pointer under
   Slang's layout rules. `MaterialSchema`'s offsets/sizes must be the *only* source
   of truth for writing into the blob — **never** `offsetof` on a mirrored C++
   struct. Reflection already gives us this; just never tempt yourself.

2. **Hot-reload changes the schema.** Unity keys saved values by *name* and rebinds
   on reload, dropping vanished fields and defaulting new ones. We have `[Default]`,
   so this is straightforward — but the serialized form must be `{name: value}`,
   **not** a byte blob, or hot-reload and asset compatibility both break.

3. **Typed setters.** `set<T>(name, v)` looks the field up in the schema, verifies
   `kind`/`scalarKind`/`elementCount` (e.g. `Vector` + `Float32` + 4 for a color),
   then memcpys at `byteOffset`. Fail loudly in debug.

4. **Texture keepalive.** `TextureHandle` is packed image+sampler indices — a plain
   `u32` in the blob. The instance must hold `ref<Texture>` alongside the blob *and*
   re-pack the handle before upload, in case the texture's heap slot moved. Cheap,
   but skipping it gives a use-after-free that only appears when a texture streams
   out.

5. **The inspector falls out for free.** Walk `MaterialSchema::fields`, switch on
   `kind`/`attributes` → `ColorEdit4` for `[Color]`, `SliderFloat` for
   `[FloatRange]`, texture picker for `TextureHandle`. Replaces
   `PBRMaterial::inspect()` entirely and works for every shader anyone writes.

## Open question to settle before step 1

**Does a `MaterialTemplate` own one PSO, or a `map<PassID, PSO>`?**

A material rendered into gbuffer vs. shadow map vs. picking needs different
attachment formats and depth state from the same shader. Unity's answer is multiple
passes per shader. A map is barely more code now; threading it through later touches
every call site.

## Implementation order

1. **`MaterialTemplate`** — wraps `ShaderProgram` + `MaterialSchema`, cached by
   path. Validates the schema is present.
2. **`Material`** (concrete, `final`) — blob + typed `get`/`set` by name, driven
   purely by schema offsets.
3. **Generic schema-driven inspector.** ← **stop here and look at it.** This is
   where you find out whether the attribute vocabulary (`[Color]`, `[FloatRange]`,
   `[Source]`, `[Default]`) is right, while changing it is still free.
4. **Texture fields** — `ref` keepalive + handle re-resolve on upload.
5. **`MaterialHeap`** (V1: per-frame arena, copy everything). Wire binding to write
   `{objectPtr, materialPtr}`.
6. **Delete `Material`/`PBRMaterial`**; move render state (`isDeferred`, cull, depth)
   to shader attributes.

Steps 1–3 touch **zero** renderer code. That's where the leverage is.
