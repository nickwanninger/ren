# Unified render graph plan

Status: agreed design and implementation plan. No engine changes yet.

## Objective

Give Renderer one persistent RenderGraph. Let independent systems create scopes,
tasks, and resources in that graph. Collect execution requests for each frame,
then prepare, compile, and record them in one place.

Replace the current graph path. Preserve typed tasks such as GBufferTask and
SSAOTask, and preserve lambda tasks. Use a strict handoff between updates and
recording. Threads, schedule caching, and multiple queues remain future work.

## Evidence from the current code

- `Viewport.cpp`: each viewport owns a graph, updates its resources, binds its
  globals, and calls `graph.run(renderer)`.
- `RenderGraph.cpp`: `run()` compiles and records into the current frame's
  encoder. Renderer already submits that frame in `endFrame()`.
- `RenderGraph.cpp`: the compiler sorts tasks, computes parallel levels, and
  sorts again. Readers depend on the last writer, so intermediate contents
  cannot be selected explicitly.
- `Resource.h`: resources store raw task pointers for readers and writers.
  There is no task or resource removal contract.
- `ImageResource.cpp`: resize replaces image storage and can wait for device
  idle before removing an ImGui descriptor.
- `Schedule.h`: generated barriers inherit RenderTask and retain graph access.
- `SubmissionUnit.h`: submissions own arenas and descriptors but lack a general
  collection of retained graph storage.
- `apps/raytracer/raytracer.cpp`: the raytrace lambda creates sceneColor, which
  bloom consumes. This provides the first temporal image example.

## Ownership and selection

Renderer owns the graph. A GraphScope groups tasks for logical ownership and
convenient enqueue. It has no scheduling boundary or viewport semantics.

The graph registers tasks when they are created. Registration alone does not
execute them. Enqueue marks a task, or every task in a scope, as an execution
root for the current frame. Follow current-version dependencies to select
required producers. Deduplicate by task identity, including across scopes.

A shared world task therefore runs once per frame when any selected task needs
it. It must be one shared task instance, not separate equivalent instances.
Selected tasks receive one execution opportunity. A task can skip commands,
but it remains responsible for valid outputs. There is no separate result
publication signal in this cut.

Scopes retain tasks. Tasks retain resource handles. The graph uses weak task
registration and does not keep unused resources alive indefinitely. Handles
do not strongly retain producer tasks. Removing a producer causes compilation
to fail when a selected consumer needs its current output. Retained history
does not require a live producer.

Enqueue retains selected roots for the frame. Selection retains prerequisites.
Frame submissions retain selected tasks, captured view state, and actual GPU objects
until their completion fence signals. Remove expired registry entries without
leaving raw reader or writer pointers behind.

## Handles and resource storage

Keep GraphHandle as the public abstraction. Replace its exposed integer model
with an opaque, copyable reference to a logical resource version. Use internal
IDs for diagnostics. Validate graph identity and resource type on lookup.

Separate resource definitions from storage:

- A definition contains the resource type, specification, and temporal policy.
- A version identifies contents and its producer.
- Storage contains an actual allocation or CPU object.
- A frame fixes the mapping from selected versions to storage.

Versions retain definitions without ownership cycles. A frame retains concrete
storage, including images, views, RenderTargets, framebuffer objects, and ImGui
descriptors. Retaining a mutable wrapper alone is insufficient after resize.

Provide a custom resource interface for allocation and transition generation.
Keep one GraphAccess enum. Tasks declare reads, writes, and updates through
handles; the graph derives dependencies. Each resource receives access history
and can append synchronization operations. CPU resources can emit no GPU
operations. Actual buffer support and CPU/GPU readback remain outside this cut.

## Writes and updates

Attachment creation establishes the initial producer and allocation authority.
An update consumes a version and returns a new version of the same allocation:

```cpp
GraphHandle scene = geometry.color();
GraphHandle outlined = outline.updates(scene);
GraphHandle final = overlay.updates(outlined);
```

One version can have many readers but only one successor update. Reject a
second successor during construction. Report the resource and both tasks.

A reader follows its version's producer. An updater follows that producer and
all selected readers of the prior version. These edges prevent overwrite
before those reads finish. Readers do not select unrelated readers or successor
updates. Reject cycles, including cycles caused by an old-version read after
a task requires the updated version.

Updating an attachment preserves its previous contents when the pass requires
them. Configure load/store behavior accordingly. Allocation authority remains
with the creator; updates do not change attachment size or format.

## Task lifecycle and view context

Use these phases:

1. Update: create/remove tasks, declare fixed dependencies, update CPU view
   contexts, and enqueue work.
2. Select: resolve producers, validate, and determine task order.
3. Capture views: each selected ViewContext stamps its values into frame-owned
   state and FrameGlobals storage before graph preparation starts.
4. Prepare: call prepareFrame once per selected task in dependency order.
5. Compile transitions: resolve resource accesses and append synchronization.
6. Record: execute the schedule and then the fullscreen/ImGui pass.
7. Submit: retain frame objects until completion.

Typed tasks override optional `prepareFrame(PrepareContext&)` and required
`run(RecordContext&) const`. Lambda tasks accept an optional preparation
callback and a recording callback with the same contexts.

Preparation can copy camera/settings into task state, resize outputs, and
prepare targets. It cannot add tasks or change dependencies. A producer's
storage is ready before a consumer prepares. Changing dependencies requires
task replacement before selection.

RecordContext exposes resolved resources and recording operations, without
graph mutation APIs. Phase violations throw. Const is shallow: captured
references and mutable pointees still require the handoff rule. Do not claim
that const alone enforces thread safety. Keep execution metrics outside the
const recording implementation.

Use one concrete, optional ViewContext. A scope supplies a default at task
construction; each task retains its assigned reference. ViewContext is mutable
CPU configuration. Before graph preparation, capture each distinct selected
context once. The capture copies its values and stamps out FrameGlobals in
storage owned by that frame, with a descriptor binding for that storage.

Both preparation and recording automatically receive const access to the
captured view state, not the live ViewContext. Bind its captured FrameGlobals
before each task. Bind captured frame defaults for tasks without a view.
Resource sizing and shader globals therefore use the same captured values.

After capture, the owner can change ViewContext values without changing the
prepared, recorded, or submitted frame. Those changes apply to a later capture.
Do not overwrite or recycle captured globals or their descriptors until the
frame completes. This removes any requirement to freeze the live ViewContext
while a frame is in flight. Concurrent mutation during capture remains outside
this single-threaded first cut.

ViewContext contains camera and render size, and can reference shared scene
data. No shared world implementation is required now. Independent views can
share scene data without sharing camera state. Graph scheduling does not
depend on context ownership. Capture references to the concrete scene storage
used by the frame. Copying a reference to a mutable wrapper does not isolate
later replacement or mutation of its contents.

Remove the graph-wide render size. Producers use prepared sizes; derived
outputs can use input dimensions. Validate framebuffer attachment compatibility.

Declare view-relative image sizes at construction, such as
`ImageSize::viewRelative(0.5f)` for SSAO. During preparation, the base render-pass
task resolves the expression against its captured view size, prepares attachment
storage, and prepares its RenderTarget. Individual tasks need no resize callback
for this case. Specify one rounding rule and clamp dimensions to at least one
pixel. A view-relative resource without an assigned view is invalid. Earlier
frames retain old storage after resize.

## Simple compiler

Rebuild the schedule every frame. Use separate, inspectable stages:

1. Collect roots and producer closure, once per task.
2. Build producer/read/update edges from fixed declarations.
3. Repeatedly scan tasks in stable registration order and select tasks whose
   prerequisites are complete. If progress stops, throw with blocked tasks and
   relevant resources. An empty frame is valid.
4. Capture view state and FrameGlobals, then prepare tasks in that order and
   fix storage mappings.
5. Walk accesses and ask resources to append transitions before task execution.
6. Append final image transitions for fullscreen/ImGui access.

Use distinct schedule operations for ExecuteTask and ResourceTransition.
Resources can supply custom transition operations. Do not represent barriers
as graph tasks. Remove parallel levels and duplicate sorting.

Track access history per actual storage, including layout and all relevant
readers before a write. Equal GraphAccess values do not eliminate write hazards.
Account for attachment transitions consistently with render-pass behavior.
Preserve state across submissions on the single graphics queue; do not reset
existing allocations to a fabricated initial state each frame.

Validation fails before recording where possible. On preparation or recording
failure, discard that frame's graph work and restore the update phase. Do not
submit a partial graph or advance temporal history. Commit recorded resource
state only for successful submissions. Integrate failure cleanup with swapchain
acquisition so an error does not strand a frame slot.

## Temporal history

Enable temporal support explicitly at resource construction. Support one prior
result. A previous-result GraphHandle resolves to storage from the producer's
last execution, not necessarily the previous Renderer frame. It does not select
the producer for the current frame.

History preserves the final contents of the allocation from the previous
producing frame, including selected updates to that allocation. Reads do not
advance history. Logical versions within that frame do not create intermediate
snapshots: preserving contents before an update requires separate storage or
an explicit copy. A previous-result handle selects this allocation history,
not the intermediate contents associated with a particular logical version.
Reject requests for unsupported intermediate snapshots.

First-use history is null. A size/specification change invalidates history.
Preparation can resolve history as an optional/nullable resource and select a
path that does not sample it. Dependencies remain fixed.

Pin previous and current mappings for the entire frame. Current output uses
different storage from the prior result. The graph manages history rotation;
custom types create their own storage. Publish the new history on successful
submission. A frame that does not select the producer leaves history unchanged.

Double-buffer temporal storage: alternate between the history and one other
allocation of the same specification. Do not allocate new storage each frame.
Work on the single graphics queue runs in submission order, and recorded
transitions order the new write after earlier reads of that allocation, so an
unfinished submission does not block reuse. Reference counts prevent
destruction. A size/specification change drops the old pair; submissions
retain it until they complete. Release bindless heap slots when storage is
destroyed. Multiple queues require a different reuse policy.

Use the raytrace sceneColor as the example. Display its previous result through
ImGui. Do not add accumulation, TAA, or camera-motion invalidation logic.

## Window output integration

The graph handles offscreen rendering in this cut. The window owns the
fullscreen pass, including output blits and ImGui recording. Do not add a
presentation task to the graph. Viewports provide output handles and do not
depend on their destination.

After graph preparation, the window resolves output handles to this frame's
prepared storage. Resolve outputs before final transition compilation so those
accesses are included. Record offscreen work and final transitions before the
window records its fullscreen pass. Use the same lookup contract for a blit,
an inspector image, or a viewport image.

Preserve final shader-read transitions for frame images. Do not require explicit
export declarations. ImGui can display any available graph image, including
history and the last output of a scope that is not enqueued.

Image lookup for display automatically retains the resolved storage and adds
it to the frame's final transition set. It does not select its producer.
Reject an unresolved current image; return null for unavailable history.

Callers supply requested sizes and enqueue work before preparation. External
consumers capture concrete images and descriptors only after preparation.
Keep this contract independent of ImGui layout and draw-list APIs. Adapt the
window and UI call sites to consume prepared outputs; the graph does not
manage UI layout, clipping, or deferred image commands. Preserve the same
frame's dimensions and do not retain stale descriptors after resize.

Use deferred image resolution for ImGui. During layout, an adapter accepts a
GraphHandle and returns an ImTextureRef from a stable ImTextureData entry.
After graph preparation, resolve the handle to an actual image descriptor,
call SetTexID on that entry, and register external sampling before final
transitions. Keep this adapter outside the graph. A fullscreen blit resolves
its handle directly and needs no ImGui adapter.

The installed Vulkan backend obtains each descriptor from `pcmd->GetTexID()`
at recording time (`imgui_impl_vulkan.cpp`). This provides the resolution
boundary. GetTexRef stores a pointer to ImTextureData; GetTexID reads its TexID
at recording time. Use this indirection instead of synthetic tokens or draw
command patching. It is not a callback: populate the descriptor before backend
recording. Keep entries at stable addresses through all relevant backend calls,
including detached windows. Do not overwrite an entry for a later frame before
the current frame's CPU recording completes.

Verify ImTextureData status and backend lifecycle handling during implementation.
The graph owns the image; the adapter owns the ImGui descriptor binding. Avoid
duplicate upload or destruction through ImGui's managed texture paths. Preserve
font and ordinary user textures. Missing history uses a valid placeholder
texture. Frames retain descriptors and images until GPU completion even after
CPU texture references are no longer needed.

Pin the displayed previous result before history advances. Extend the retention
set through the fullscreen pass. Updating the imnodes dependency view is not
required; remove or adapt old API references so the inspector still builds.

## Implementation sequence

1. Add resource definitions, version handles, storage references, weak producer
   registration, and validation. Add CPU tests for version chains and lifetime.
2. Add scopes, explicit enqueue, and producer selection. Add the simple ordering
   compiler and distinct schedule operations. Test selection and ordering without
   Vulkan. Keep temporary migration adapters only where needed for buildable steps.
3. Add PrepareContext, const recording, optional lambda preparation, and
   ViewContext capture. Stamp FrameGlobals into frame-owned storage before
   preparation. Adapt typed tasks and image/target preparation. Add frame
   retention and release it only after fence completion.
4. Move graph ownership and execution into Renderer. Migrate viewport/app call
   sites and split UI collection from prepared image lookup. Remove per-viewport
   execution and graph-wide sizing. Verify one central recording path.
5. Implement temporal storage selection, history invalidation, and the raytracer
   ImGui example. Verify storage reuse under multiple frames in flight.
6. Remove the old compiler, barrier tasks, resource pointer lists, and obsolete
   preparation APIs. Remove resize idle waits only after retention covers every
   affected object. No permanent dual graph path remains.

## Validation

Use the existing ren-tests target and CTest integration. Add focused tests for:

- Shared producers execute once across two scopes; unused tasks do not execute.
- Missing producers, foreign handles, invalid types, branched updates, and cycles
  throw useful exceptions. Empty selection succeeds.
- Reads of old contents precede an update; reads of new contents follow it.
- Preparation follows producer order; dependency mutation during preparation fails.
- Scope removal releases unused resources; submitted frames retain actual storage.
- Different views use their own sizes and globals, even when task order interleaves.
- Changing a live ViewContext after capture leaves that frame's prepared sizes,
  camera values, GPU globals, and descriptors unchanged. The next capture sees
  the changes. Verify this with multiple frames in flight and shared contexts.
- Half-resolution SSAO follows the captured view size, including odd and small
  dimensions. Deferred ImGui references resolve to the resized allocation in
  that same frame; ordinary textures and detached windows remain valid.
- Custom resources append transitions; same-access writes still request synchronization.
- History is null initially and after resize, remains stable without producer
  execution, and advances only after successful submission.
- History contains the prior producing frame's final contents after selected
  updates. Reads do not advance it, and current updates do not change the
  previous-result mapping pinned for that frame.
- Failed frames do not publish history or recorded access state.

Build the engine and raytracer. Run with Vulkan validation and exercise two
viewports, repeated resize, close/reopen, an unselected scope shown in the
inspector, and current/previous raytrace image display. Verify descriptor,
framebuffer, and image lifetimes with frames in flight. Observe stable storage
usage after a bounded resize sequence completes.

Completion means one Renderer graph path, all existing rendering paths migrated,
the temporal display example works, and no validation errors occur in these
scenarios. Performance tuning is not an acceptance requirement.

## Deferred work

Threads, schedule caching, parallel recording, separate compute queues, CPU/GPU
readback, concrete buffer resources, shared world implementation, accumulation,
multiple history depths, memory alias optimization, and imnodes redesign.
