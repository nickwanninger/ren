#include <ren/renderer/graph/RenderGraph.h>
#include <iostream>
#include <functional>

using namespace ren;
namespace {
  void verify(bool value, const char *message) {
    if (!value) {
      throw std::runtime_error(message);
    }
  }
  void throws(const std::function<void()> &action, const char *message) {
    try {
      action();
    } catch (const std::runtime_error &error) {
      verify(std::string(error.what()).find(message) != std::string::npos, error.what());
      return;
    }
    throw std::runtime_error("Expected an exception");
  }
  struct Storage : GraphStorage {
    glm::uvec2 size;
    int value = 0;
    explicit Storage(glm::uvec2 size)
        : size(size) {}
    void transition(RecordContext &, const std::vector<GraphAccess> &, GraphAccess) const override {}
  };
  struct Definition : GraphResource {
    ImageSize size = ImageSize::absolute(glm::uvec2(4));
    ref<GraphStorage> allocate(const CapturedView *view) const override { return make<Storage>(size.resolve(view)); }
    bool compatible(const GraphStorage &storage, const CapturedView *view) const override {
      return dynamic_cast<const Storage &>(storage).size == size.resolve(view);
    }
  };
  struct Task : RenderTask {
    using RenderTask::RenderTask;
    std::function<void(PrepareContext &)> preparation;
    void prepareFrame(PrepareContext &ctx) override {
      if (preparation) {
        preparation(ctx);
      }
    }
    void run(RecordContext &) const override {}
  };
  GraphHandle output(RenderGraph &graph, Task &task, ref<Definition> definition = make<Definition>()) {
    auto handle = graph.createResource("test storage", definition);
    task.write(handle, GraphAccess::ComputeShaderWrite);
    return handle;
  }
  void publish(RenderGraph &graph) {
    graph.compileTransitions();
    graph.submitted();
  }
  void selection() {
    RenderGraph graph;
    GraphScope world(graph), left(graph), right(graph);
    auto &producer = world.addTask<Task>("world");
    auto value = output(graph, producer);
    auto &a = left.addTask<Task>("left");
    a.read(value, GraphAccess::ShaderRead);
    auto &b = right.addTask<Task>("right");
    b.read(value, GraphAccess::ShaderRead);
    world.addTask<Task>("unused");
    verify(graph.compile().tasks.empty(), "Empty selection must succeed");
    left.enqueue();
    right.enqueue();
    graph.enqueue(a);
    auto selected = graph.compile().tasks;
    verify(selected.size() == 3 && selected[0].get() == &producer, "Shared producer must occur once");
  }
  void versions() {
    RenderGraph graph;
    GraphScope scope(graph);
    auto &producer = scope.addTask<Task>("producer");
    auto first = output(graph, producer);
    auto &update = scope.addTask<Task>("update");
    auto second = update.updates(first, GraphAccess::ComputeShaderWrite);
    auto &oldReader = scope.addTask<Task>("old reader");
    oldReader.read(first, GraphAccess::VertexShaderRead);
    auto &newReader = scope.addTask<Task>("new reader");
    newReader.read(second, GraphAccess::ShaderRead);
    scope.enqueue();
    auto selected = graph.compile().tasks;
    verify(selected.size() == 4 && selected[1].get() == &oldReader && selected[2].get() == &update && selected[3].get() == &newReader,
           "Old reads must finish before update and new reads follow it");
    auto &branch = scope.addTask<Task>("branch");
    throws([&] { branch.updates(first); }, "Branched update");
    graph.discard();
    graph.enqueue(oldReader);
    verify(graph.compile().tasks.size() == 2, "Old reader must not select successor or unrelated readers");
    graph.discard();
    throws([&] { oldReader.read(second, GraphAccess::ShaderRead); }, "dependencies are fixed");
    RenderGraph cyclic;
    GraphScope cycleScope(cyclic);
    auto &initial = cycleScope.addTask<Task>("initial");
    auto old = output(cyclic, initial);
    auto &writer = cycleScope.addTask<Task>("writer");
    auto next = writer.updates(old, GraphAccess::ComputeShaderWrite);
    auto &reader = cycleScope.addTask<Task>("reader");
    reader.read(old, GraphAccess::ShaderRead);
    reader.read(next, GraphAccess::ShaderRead);
    cycleScope.enqueue();
    throws([&] { cyclic.compile(); }, "cycle");
  }
  void invalid() {
    RenderGraph graph, foreign;
    GraphScope scope(graph), other(foreign);
    auto &producer = scope.addTask<Task>("producer");
    auto handle = output(graph, producer);
    auto &task = other.addTask<Task>("foreign");
    throws([&] { task.read(handle, GraphAccess::ShaderRead); }, "foreign");
    throws([&] { graph.get<ImageResource>(handle); }, "type");
    throws([&] { graph.getResource({}); }, "Invalid");
    throws([&] { foreign.enqueue(producer); }, "foreign");
    throws([&] { graph.previous(handle); }, "temporal");
    auto missing = graph.createResource("no producer", make<Definition>());
    auto &reader = scope.addTask<Task>("reader");
    reader.read(missing, GraphAccess::ShaderRead);
    graph.enqueue(reader);
    throws([&] { graph.compile(); }, "missing producer");
  }
  void lifetime() {
    RenderGraph graph;
    GraphScope producerScope(graph), consumerScope(graph);
    auto &producer = producerScope.addTask<Task>("producer");
    auto handle = output(graph, producer);
    auto &consumer = consumerScope.addTask<Task>("consumer");
    consumer.read(handle, GraphAccess::ShaderRead);
    producerScope.clear();
    graph.enqueue(consumer);
    throws([&] { graph.compile(); }, "missing producer");
    graph.discard();
    auto definition = make<Definition>();
    std::weak_ptr<GraphResource> weak = definition;
    auto &retained = producerScope.addTask<Task>("retained");
    auto retainedHandle = output(graph, retained, definition);
    definition.reset();
    graph.enqueue(retained);
    producerScope.clear();
    retainedHandle = {};
    verify(!weak.expired(), "Enqueued task must retain its resource");
    graph.discard();
    graph.beginFrame();
    verify(weak.expired(), "Weak registry must release unused resources");
  }
  void prepareAndCapture() {
    RenderGraph graph;
    auto view = make<ViewContext>();
    view->renderSize = {7, 3};
    view->view[3][0] = 17;
    GraphScope scope(graph, view);
    auto definition = make<Definition>();
    definition->size = ImageSize::viewRelative(0.5f);
    auto &producer = scope.addTask<Task>("producer");
    auto handle = output(graph, producer, definition);
    auto &consumer = scope.addTask<Task>("consumer");
    consumer.read(handle, GraphAccess::ShaderRead);
    const CapturedView *captured = nullptr;
    producer.preparation = [&](PrepareContext &ctx) {
      captured = ctx.view;
      verify(ctx.resources.get<Storage>(handle)->size == glm::uvec2(3, 1), "Half size must round down and clamp");
      view->renderSize = {100, 100};
      view->view[3][0] = 99;
      throws([&] { consumer.read(handle, GraphAccess::ShaderRead); }, "update phase");
    };
    consumer.preparation = [&](PrepareContext &ctx) {
      verify(ctx.view == captured && ctx.view->values.renderSize == glm::uvec2(7, 3), "Shared capture must remain fixed");
      verify(ctx.view->globals.viewMatrix[3][0] == 17 && ctx.view->globals.renderSize == glm::vec2(7, 3),
             "Captured globals must use captured camera and size");
    };
    graph.enqueue(consumer);
    graph.prepareFrame();
    auto retained = graph.resolve(handle);
    publish(graph);
    producer.preparation = {};
    consumer.preparation = {};
    graph.enqueue(consumer);
    graph.prepareFrame();
    verify(std::dynamic_pointer_cast<Storage>(graph.resolve(handle))->size == glm::uvec2(50), "Next capture must see live changes");
    verify(std::dynamic_pointer_cast<Storage>(retained)->size == glm::uvec2(3, 1), "Old storage must survive resize");
    graph.discard();
    CapturedView tiny;
    tiny.values.renderSize = {1, 1};
    verify(ImageSize::viewRelative(0.5f).resolve(&tiny) == glm::uvec2(1), "Tiny dimensions must clamp");
    throws([&] { ImageSize::viewRelative(1).resolve(nullptr); }, "ViewContext");
  }
  void independentViews() {
    RenderGraph graph;
    auto a = make<ViewContext>(), b = make<ViewContext>();
    a->renderSize = {5, 7};
    b->renderSize = {9, 11};
    GraphScope left(graph, a), right(graph, b);
    auto &l = left.addTask<Task>("left");
    auto ld = make<Definition>();
    ld->size = ImageSize::viewRelative(1);
    auto lh = output(graph, l, ld);
    auto &r = right.addTask<Task>("right");
    auto rd = make<Definition>();
    rd->size = ImageSize::viewRelative(1);
    auto rh = output(graph, r, rd);
    r.read(lh, GraphAccess::ShaderRead);
    right.enqueue();
    graph.prepareFrame();
    verify(std::dynamic_pointer_cast<Storage>(graph.resolve(lh))->size == glm::uvec2(5, 7), "Left view size mismatch");
    verify(std::dynamic_pointer_cast<Storage>(graph.resolve(rh))->size == glm::uvec2(9, 11), "Right view size mismatch");
    graph.discard();
  }
  void transitions() {
    RenderGraph graph;
    GraphScope scope(graph);
    auto &producer = scope.addTask<Task>("producer");
    auto handle = output(graph, producer);
    auto &update = scope.addTask<Task>("update");
    update.updates(handle, GraphAccess::ComputeShaderWrite);
    scope.enqueue();
    graph.prepareFrame();
    auto storage = graph.resolve(handle);
    auto &schedule = graph.compileTransitions();
    verify(schedule.operations.size() == 4, "Writes require separate transition operations");
    const auto &barrier = std::get<ScheduledResourceTransition>(schedule.operations[2]);
    verify(barrier.from == std::vector{GraphAccess::ComputeShaderWrite} && barrier.to == GraphAccess::ComputeShaderWrite,
           "Equal access writes still require synchronization");
    graph.discard();
    verify(storage->access.empty(), "Failed frame must not publish access state");
    scope.enqueue();
    graph.prepareFrame();
    storage = graph.resolve(handle);
    publish(graph);
    verify(storage->access == std::vector{GraphAccess::ComputeShaderWrite}, "Successful submission must preserve access state");
    scope.enqueue();
    graph.prepareFrame();
    auto &next = graph.compileTransitions();
    verify(!std::get<ScheduledResourceTransition>(next.operations[0]).from.empty(), "Next submission must use actual prior state");
    graph.discard();
  }
  void history() {
    RenderGraph graph;
    auto view = make<ViewContext>();
    view->renderSize = {5, 7};
    GraphScope scope(graph, view);
    auto definition = make<Definition>();
    definition->temporal = true;
    definition->size = ImageSize::viewRelative(1);
    auto &producer = scope.addTask<Task>("producer");
    auto handle = output(graph, producer, definition);
    auto previous = graph.previous(handle);
    auto &update = scope.addTask<Task>("update");
    auto final = update.updates(handle, GraphAccess::ComputeShaderWrite);
    update.preparation = [&](PrepareContext &ctx) { ctx.resources.get<Storage>(final)->value = 42; };
    scope.enqueue();
    graph.prepareFrame();
    verify(!graph.resolve(previous), "Initial history must be null");
    auto first = graph.resolve(handle);
    publish(graph);
    graph.prepareFrame();
    verify(graph.resolve(previous) == first && graph.compile().tasks.empty(), "History alone must not select producer");
    publish(graph);
    scope.enqueue();
    graph.prepareFrame();
    auto second = graph.resolve(handle);
    verify(second != first && graph.resolve(previous) == first, "Current allocation must differ from pinned history");
    verify(std::dynamic_pointer_cast<Storage>(graph.resolve(previous))->value == 42, "History must contain final updated contents");
    graph.discard();
    graph.prepareFrame();
    verify(graph.resolve(previous) == first, "Failed frame must not publish history");
    graph.discard();
    scope.enqueue();
    graph.prepareFrame();
    second = graph.resolve(handle);
    publish(graph);
    graph.prepareFrame();
    verify(graph.resolve(previous) == second, "Successful producer execution must advance history");
    graph.discard();
    view->renderSize = {3, 1};
    scope.enqueue();
    graph.prepareFrame();
    verify(!graph.resolve(previous), "Resize must invalidate history");
    graph.discard();
    producer.preparation = [](PrepareContext &) { throw std::runtime_error("prepare failure"); };
    scope.enqueue();
    throws([&] { graph.prepareFrame(); }, "prepare failure");
    graph.prepareFrame();
    verify(graph.resolve(previous) == second, "Preparation failure must preserve published history");
    graph.discard();
    scope.clear();
    graph.prepareFrame();
    verify(graph.resolve(previous) == second, "History must survive producer removal");
    graph.discard();
  }
}  // namespace
int main() {
  try {
    selection();
    versions();
    invalid();
    lifetime();
    prepareAndCapture();
    independentViews();
    transitions();
    history();
    std::cout << "Render graph tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
