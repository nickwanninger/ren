#pragma once
#include <ren/renderer/graph/RunContext.h>
#include <functional>

namespace ren {
  class RenderGraph;
  class GraphScope;



  // A RenderTask is one unit of GPU work in the graph, e.g. a render pass or a
  // compute dispatch. When the task is built, it declares the resources that it
  // reads, writes, and updates, and the graph orders tasks by these
  // declarations. In each frame where the task is selected, the graph calls
  // prepareFrame() and then run(). Make tasks with GraphScope::addTask() or
  // with a TaskBuilder.
  class RenderTask {
   public:
    explicit RenderTask(RenderGraph &graph);
    virtual ~RenderTask() = default;


    // Get ready for this frame: copy settings, and make the objects that
    // recording uses. The storage of each input is ready. Dependencies cannot
    // change here.
    virtual void prepareFrame(PrepareContext &) {}

    // Record the commands of this task. Recording should not change state;
    // per-frame state belongs in prepareFrame().
    virtual void run(RecordContext &) const = 0;

    // Show the settings of the task in ImGui.
    virtual void inspect() {}


    const std::string &name() const { return taskName; }
    void setName(std::string value) { taskName = std::move(value); }


    // Declare how the task uses resources. Call these while the task is built,
    // in the update step. After the task is first selected, its dependencies
    // are fixed and these calls throw. write() makes this task the producer of
    // the version, and throws if the version already has a producer.
    RenderTask &read(GraphHandle, GraphAccess);
    RenderTask &write(GraphHandle, GraphAccess);

    // Updating is like "add to every pixel": the task changes the contents of a
    // version, and returns the handle of the new version. The new version uses
    // the same storage. Readers of the old version run first. A version can
    // have only one update.
    GraphHandle updates(GraphHandle, GraphAccess = GraphAccess::RenderTarget);

    // The declared inputs and outputs of the task.
    const auto &getOperands() const { return operands; }
    const auto &getResults() const { return results; }
    RenderGraph &graph() const { return owner; }

    // The graph calls this to record the task. It calls run() and measures the
    // CPU time of recording.
    void execute(RecordContext &);

   private:
    RenderGraph &owner;
    u64 id;               // Unique in the graph, and never used again.
    bool sealed = false;  // True after the first selection. The dependencies are then fixed.
    std::string taskName;

    // Inputs and outputs. An update adds its old version to the operands.
    std::vector<GraphHandleUsage> operands, results;
    ref<ViewContext> view;  // From the GraphScope. Can be null.

    // The CPU time of recording. Nothing shows these yet.
    double totalTimeNs = 0;
    u64 numExecutions = 0;

    friend class RenderGraph;
    friend class GraphScope;
  };



  using RenderTaskLambda = std::function<void(RecordContext &)>;
  using PrepareTaskLambda = std::function<void(PrepareContext &)>;


  // This class provides a builder pattern to define tasks in the render graph
  // using lambda callbacks, not subclassing. It's just a little nicer sometimes.
  // The builder adds nothing to the graph until execute() or render() is
  // called. A builder that is destroyed before that adds no task.
  class TaskBuilder {
   public:
    TaskBuilder(GraphScope &, std::string);
    TaskBuilder(const TaskBuilder &) = delete;
    TaskBuilder &operator=(const TaskBuilder &) = delete;
    TaskBuilder(TaskBuilder &&) = default;


    // Declare that the task will read or write a resource. The declarations
    // apply when the task is made.
    TaskBuilder &reads(GraphHandle, GraphAccess);
    TaskBuilder &writes(GraphHandle, GraphAccess);

    // Provide a lambda callback that prepares the task before execution. The
    // run method is const, but const does not apply to the captures of a
    // lambda. Preparation is where per-frame state belongs, because recording
    // should not change state.
    TaskBuilder &prepare(PrepareTaskLambda);

    // Add color or depth attachments to a render pass. The pass clears each
    // one to its clear value. The output handle is written when render() is
    // called, so it stays null until then. execute() throws if the builder has
    // attachments.
    TaskBuilder &createColorAttachment(std::string_view, const GraphImageSpec &, GraphHandle &, glm::vec4 clearColor = glm::vec4(0.0f));
    TaskBuilder &createDepthAttachment(std::string_view, const GraphImageSpec &, GraphHandle &, float clearDepth = 1.0f);

    // Finalizer/build methods. A builder can finalize only once.
    // Execute means the task runs the lambda without a render pass, e.g. for
    // compute, copies, or blits.
    RenderTask &execute(RenderTaskLambda);
    // Render means it is a render pass task that runs the lambda with a RenderPassEncoder.
    RenderTask &render(std::function<void(GraphRenderPassContext &)>);

   private:
    struct Attachment {
      std::string name;
      GraphImageSpec spec;
      GraphHandle *output;
      bool depth;
      glm::vec4 clearColor;  // For a color attachment.
      float clearDepth;      // For a depth attachment.
    };
    GraphScope &scope;
    std::string name;
    std::vector<GraphHandleUsage> readsList, writesList;
    std::vector<Attachment> attachments;
    PrepareTaskLambda preparation;
    bool finalized = false;
    void apply(RenderTask &);
  };
}  // namespace ren
