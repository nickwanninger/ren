#include <ren/renderer/graph/RenderGraph.h>
#include <ren/renderer/graph/RenderPassTask.h>

namespace ren {
  namespace {


    // A LambdaTask is a RenderTask that runs a lambda. Importantly it is not
    // wrapped in a render pass, and should be used for compute.
    class LambdaTask : public RenderTask {
     public:
      LambdaTask(RenderGraph &graph, RenderTaskLambda record, PrepareTaskLambda prepare)
          : RenderTask(graph)
          , record(std::move(record))
          , preparation(std::move(prepare)) {}
      void prepareFrame(PrepareContext &ctx) override {
        if (preparation) {
          preparation(ctx);
        }
      }
      void run(RecordContext &ctx) const override { record(ctx); }

     private:
      RenderTaskLambda record;
      PrepareTaskLambda preparation;
    };

    // A LambdaPass is a render pass task, which runs a lambda in a render
    // pass. (graphics)
    class LambdaPass : public RenderPassTask {
     public:
      LambdaPass(RenderGraph &graph, std::function<void(GraphRenderPassContext &)> record, PrepareTaskLambda prepare)
          : RenderPassTask(graph)
          , record(std::move(record))
          , preparation(std::move(prepare)) {}
      void prepareFrame(PrepareContext &ctx) override {
        RenderPassTask::prepareFrame(ctx);
        if (preparation) {
          preparation(ctx);
        }
      }
      void run(GraphRenderPassContext &ctx) const override { record(ctx); }

     private:
      std::function<void(GraphRenderPassContext &)> record;
      PrepareTaskLambda preparation;
    };


  }  // namespace



  TaskBuilder::TaskBuilder(GraphScope &scope, std::string name)
      : scope(scope)
      , name(std::move(name)) {}

  TaskBuilder &TaskBuilder::reads(GraphHandle handle, GraphAccess access) {
    readsList.push_back({handle, access});
    return *this;
  }


  TaskBuilder &TaskBuilder::writes(GraphHandle handle, GraphAccess access) {
    writesList.push_back({handle, access});
    return *this;
  }


  TaskBuilder &TaskBuilder::prepare(PrepareTaskLambda value) {
    preparation = std::move(value);
    return *this;
  }


  TaskBuilder &TaskBuilder::createColorAttachment(std::string_view name, const GraphImageSpec &spec, GraphHandle &output, glm::vec4 clearColor) {
    attachments.push_back({std::string(name), spec, &output, false, clearColor, 1.0f});
    return *this;
  }


  TaskBuilder &TaskBuilder::createDepthAttachment(std::string_view name, const GraphImageSpec &spec, GraphHandle &output, float clearDepth) {
    attachments.push_back({std::string(name), spec, &output, true, glm::vec4(0.0f), clearDepth});
    return *this;
  }


  void TaskBuilder::apply(RenderTask &task) {
    for (const auto &input : readsList) {
      task.read(input.handle, input.access);
    }
    for (const auto &output : writesList) {
      task.write(output.handle, output.access);
    }
  }


  RenderTask &TaskBuilder::execute(RenderTaskLambda callback) {

    // We can only finalize once.
    if (finalized) {
      throw std::runtime_error("Task builder already finalized");
    }
    finalized = true;

    // This is a compute task, and thus cannot have attachments
    if (!attachments.empty()) {
      throw std::runtime_error("Attachments require a render pass");
    }

    // Construct the task.
    auto &task = scope.addTask<LambdaTask>(name.c_str(), std::move(callback), std::move(preparation));
    apply(task);
    return task;
  }


  RenderTask &TaskBuilder::render(std::function<void(GraphRenderPassContext &)> callback) {
    // Only finalize once
    if (finalized) {
      throw std::runtime_error("Task builder already finalized");
    }
    finalized = true;

    // Construct the task in the scope with its attachments.
    auto &task = scope.addTask<LambdaPass>(name.c_str(), std::move(callback), std::move(preparation));
    for (auto &attachment : attachments) {
      *attachment.output = attachment.depth ? task.addDepthAttachment(attachment.name, attachment.spec, attachment.clearDepth)
                                            : task.addColorAttachment(attachment.name, attachment.spec, attachment.clearColor);
    }
    apply(task);
    return task;
  }


}  // namespace ren
