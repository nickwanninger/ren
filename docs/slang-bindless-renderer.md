# Slang bindless renderer

REN's active shader path is native Vulkan 1.3, runtime-compiled Slang, fixed
global descriptor sets, reflected push constants, and buffer device addresses.
The direct `CommandEncoder` API is the core API. The render graph may schedule
that work later; it does not own shader parameters.

There is no GLSL compilation or per-material descriptor-set fallback.

## Required Vulkan contract

Device creation requires:

- Vulkan 1.3;
- synchronization2 and dynamic rendering support;
- descriptor indexing and runtime descriptor arrays;
- non-uniform sampled- and storage-image indexing;
- partially-bound, update-after-bind sampled/storage image descriptors;
- buffer device addresses;
- at least 128 bytes of push constants.

The current render path still uses Vulkan render passes. Dynamic rendering and
synchronization2 are required platform capabilities but their renderer-wide
adoption is deferred.

## Global shader ABI

`GlobalDescriptorABI` in
`src/ren/renderer/GlobalDescriptors.h` is the C++ source of truth. `ren.slang`
is its shader-side counterpart.

| Set | Binding | Type | Capacity | Ownership |
| --- | ---: | --- | ---: | --- |
| 0 | 0 | uniform buffer | 1 | current `SubmissionUnit` |
| 1 | 0 | sampler | 64 | renderer-global heap |
| 1 | 2 | sampled image | 4096 | renderer-global heap |
| 1 | 3 | storage image | 1024 | renderer-global heap |

Set 1 is Slang's documented typed descriptor-heap layout produced by
`defaultGetDescriptorFromHandle(..., BindlessDescriptorOptions.None)`.
`BindlessSpaceIndex` is fixed to 1 during compilation. REN deliberately does
not use mutable descriptors or reflection-generated set layouts.

Every `ShaderProgram` uses the same two descriptor-set layouts and a 128-byte
push-constant range visible to all stages. Loading a shader fails if either
Slang reflection or emitted SPIR-V declares a descriptor outside this ABI.

### Frame globals

Every Slang shader imports the shared module:

```slang
import ren;
```

That exposes `frameGlobals`:

```slang
public struct FrameGlobals
{
    public float time;
    public float deltaTime;
    public uint frameNumber;
    public uint _padding;
    public float2 renderSize;
    public float2 inverseRenderSize;
};
```

Each `SubmissionUnit` owns an immutable-size, persistently mapped uniform
buffer and its set 0 descriptor. `Renderer::beginFrame()` initializes it;
`SubmissionUnit::updateFrameGlobals()` may replace the values before encoding
work. Both graphics and compute shader binds bind sets 0 and 1 automatically.

## Bindless resources

Register resources once and pass only typed handles through push constants:

```cpp
auto& heap = renderer.getGlobalDescriptors();
auto image = heap.registerSampledImage(texture->getImage());
auto sampler = heap.registerSampler(renderer.getSampler(VK_FILTER_LINEAR));

cursor.set("image", image).set("sampler", sampler);
```

Shader fields use Slang's native handle types:

```slang
struct Constants
{
    Texture2D<float4>.Handle image;
    SamplerState.Handle sampler;
};
```

The C++ `SampledImageHandle`, `StorageImageHandle`, and `SamplerHandle` match
Slang's two-word `DescriptorHandle<T>` representation. Their types prevent
mixing heap kinds accidentally.

`GlobalDescriptors::replace(handle, resource)` updates a stable slot. This is
the intended primitive for streaming an image out to a default texture and
later replacing it in place. Image registrations retain `ref<Image>` objects.
Sampler registration does not own the sampler, so its owner must outlive the
descriptor.

Current limitations:

- slots are append-only and never recycled;
- heap capacities are fixed;
- handles have no generation counter;
- replacement synchronization and deferred destruction are the caller's
  responsibility;
- only 2D sampled/storage images and samplers are represented by the public
  C++ handle API.

## Push constants and `ShaderCursor`

A shader may declare any push-constant struct that fits the fixed 128-byte
range:

```slang
struct DrawConstants
{
    float2 offset;
    float scale;
    Texture2D<float4>.Handle image;
};

[[vk::push_constant]]
ConstantBuffer<DrawConstants> pushConstants;
```

Binding a program returns a command-scoped `ShaderCursor`. `set()` resolves a
field name through Slang reflection and immediately emits `vkCmdPushConstants`:

```cpp
auto cursor = pass.bindGraphics(pso);
cursor.set("offset", glm::vec2(-0.5f, 0.0f))
      .set("scale", 0.3f)
      .set("image", imageHandle);
pass.drawIndexed(cursor, {.vertexCount = indexCount});
```

`set()` requires the C++ value size to exactly match the reflected field.
Missing, ambiguous, or oversized fields fail immediately. REN intentionally
does not keep a parameter block, shadow copy, initialization mask, or
completeness check.

A cursor belongs to one encoder, bind point, program, and binding generation.
Binding another graphics or compute program makes the previous cursor for that
bind point stale. Passing a stale or foreign cursor to `draw`/`dispatch`
throws. The cursor does not own resource lifetimes.

The `PipelineStateObject` remains the explicit C++ source for graphics fixed
state. Slang metadata does not construct a PSO.

## Buffer device addresses

`BufferMemory` is an immutable-size, move-only RAII allocation. Every
allocation includes transfer source/destination and shader-device-address
usage. Choose the access domain explicitly:

```cpp
auto input = allocateBuffer<float>(count, BufferDomain::Upload);
auto output = allocateBuffer<float>(count, BufferDomain::Readback);

input.copyFromHost(values.data(), values.size() * sizeof(float));
cursor.set("input", input.devicePointer<float>())
      .set("output", output.devicePointer<float>());
```

- `Device`: device-local and not CPU mapped.
- `Upload`: persistently mapped, host-coherent sequential writes.
- `Readback`: persistently mapped, host-coherent cached random access.

Allocation size never changes. Code needing another size must allocate another
buffer and explicitly manage migration and lifetime. A device-local buffer
requires an explicit copy from an upload buffer; `copyFromHost()` rejects it.

The corresponding Slang field is a pointer such as `float*`. Buffer pointers
are preferred over adding per-shader storage-buffer descriptors.

## Compute

Compute uses the same program, cursor, global ABI, and push-constant machinery:

```cpp
auto program = make<ShaderProgram>("test/saxpy");
auto command = submission.begin();
auto cursor = command->bindCompute(program);
cursor.set("a", a)
      .set("length", length)
      .set("x", x.devicePointer<float>())
      .set("y", y.devicePointer<float>())
      .set("output", output.devicePointer<float>());
command->dispatch(cursor, {(length + 255) / 256, 1, 1});
submission.submitTo(*getVulkan().graphicsQueue);
```

`src/ren/res/shaders/test/saxpy.slang` and `testSaxpy()` in
`src/editor/main.cpp` are the complete executable example. Run:

```sh
dist/bin/editor --run-saxpy
```

The smoke test dispatches 10,000 elements, waits on the single graphics queue,
and validates the mapped output on the CPU.

## Graphics reference

`Application::run()` contains the first direct renderer demo. It creates two
generated square meshes, two Slang programs with different push-constant
schemas, two bindless textures, and a shared sampler. Both programs read the
same `frameGlobals`; each draw fills its own reflected cursor and uses a stable
texture handle. The demo intentionally bypasses the render graph.

Relevant shader modules:

- `src/ren/res/shaders/demo/square_a.slang`
- `src/ren/res/shaders/demo/square_b.slang`
- `src/ren/res/shaders/ren.slang`

## Deferred work

This cutover establishes the shader/resource model. It intentionally does not
solve:

- dynamic-rendering conversion;
- synchronization2 command conversion;
- swapchain-image versus in-flight-frame ownership cleanup;
- render-graph task migration or redesign;
- PBR/material restoration on the new API;
- a central shader-system/cache owner;
- compiled shader caching, hot reload, or disk artifacts;
- configurable Slang entry-point selection;
- PSO construction from shader metadata;
- descriptor slot reuse, generations, heap growth, and streaming fences;
- automatic upload staging for device-local buffers;
- multiple queues or asynchronous compute.

Keep new rendering research on the direct API. Migrate a graph task only when
it is needed, treating the graph as dependency/lifetime scheduling around the
same command operations.
