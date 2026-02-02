#include "Task.h"
#include "RenderGraph.h"
#include "RenderPassTask.h"

#include <stdexcept>

namespace ren {

  // LambdaRenderPassTask - defined here to avoid adding RenderPassTask.h dependency to RenderGraph.h
  class LambdaRenderPassTask : public RenderPassTask {
   public:
    using Callback = std::function<void(GraphRenderPassContext &)>;

    LambdaRenderPassTask(RenderGraph &graph, Callback func)
        : RenderPassTask(graph)
        , m_func(std::move(func)) {}

    void run(GraphRenderPassContext &ctx) override { m_func(ctx); }

   private:
    Callback m_func;
  };



  class LambdaComputePassTask : public RenderTask {
   public:
    using Callback = std::function<void(GraphRunContext &)>;

    LambdaComputePassTask(RenderGraph &graph, Callback func)
        : RenderTask(graph)
        , m_func(std::move(func)) {}

    void run(GraphRunContext &ctx) override { m_func(ctx); }

   private:
    Callback m_func;
  };

  // TaskBuilder implementation

  TaskBuilder::TaskBuilder(RenderGraph &graph, std::string name)
      : m_graph(&graph)
      , m_name(std::move(name))
      , m_finalized(false) {}

  TaskBuilder &TaskBuilder::reads(GraphHandle handle, GraphAccess access) {
    if (m_finalized) {
      throw std::runtime_error("TaskBuilder already finalized");
    }
    m_reads.emplace_back(handle, access);
    return *this;
  }

  TaskBuilder &TaskBuilder::reads(GraphHandleUsage resource) { return reads(resource.handle, resource.access); }

  TaskBuilder &TaskBuilder::writes(GraphHandle handle, GraphAccess access) {
    if (m_finalized) {
      throw std::runtime_error("TaskBuilder already finalized");
    }
    m_writes.emplace_back(handle, access);
    return *this;
  }

  TaskBuilder &TaskBuilder::writes(GraphHandleUsage resource) { return writes(resource.handle, resource.access); }

  TaskBuilder &TaskBuilder::createColorAttachment(const std::string_view &name, const GraphImageSpec &spec, GraphHandle &out_handle) {
    if (m_finalized) {
      throw std::runtime_error("TaskBuilder already finalized");
    }

    AttachmentSpec attachment;
    attachment.name = std::string(name);
    attachment.spec = spec;
    attachment.out_handle = &out_handle;
    attachment.is_depth = false;

    m_attachments.push_back(std::move(attachment));
    return *this;
  }

  TaskBuilder &TaskBuilder::createDepthAttachment(const std::string_view &name, const GraphImageSpec &spec, GraphHandle &out_handle) {
    if (m_finalized) {
      throw std::runtime_error("TaskBuilder already finalized");
    }

    AttachmentSpec attachment;
    attachment.name = std::string(name);
    attachment.spec = spec;
    attachment.out_handle = &out_handle;
    attachment.is_depth = true;

    m_attachments.push_back(std::move(attachment));
    return *this;
  }

  void TaskBuilder::applyDependencies(RenderTask &task) {
    // Apply all read dependencies
    for (const auto &read : m_reads) {
      task.read(read.handle, read.access);
    }

    // Apply all write dependencies
    for (const auto &write : m_writes) {
      task.write(write.handle, write.access);
    }
  }

  RenderTask &TaskBuilder::execute(std::function<void(GraphRunContext &)> func) {
    if (m_finalized) {
      throw std::runtime_error("TaskBuilder already finalized");
    }
    m_finalized = true;

    if (!m_attachments.empty()) {
      throw std::runtime_error(
          "Cannot use createColorAttachment/createDepthAttachment with execute(). "
          "Use render() for render pass tasks.");
    }

    auto &task = m_graph->addTask<LambdaComputePassTask>(m_name.c_str(), std::move(func));
    applyDependencies(task);

    return task;
  }

  RenderTask &TaskBuilder::render(std::function<void(GraphRenderPassContext &)> func) {
    if (m_finalized) {
      throw std::runtime_error("TaskBuilder already finalized");
    }
    m_finalized = true;

    // Create LambdaRenderPassTask
    auto &task = m_graph->addTask<LambdaRenderPassTask>(m_name.c_str(), std::move(func));

    // Create attachments BEFORE applying reads/writes
    // Matches pattern in typed tasks where addColorAttachment() is called
    // in constructor before manual read() calls
    for (auto &attachment : m_attachments) {
      GraphHandle handle;
      if (attachment.is_depth) {
        handle = task.addDepthAttachment(attachment.name, attachment.spec);
      } else {
        handle = task.addColorAttachment(attachment.name, attachment.spec);
      }

      // Write handle to caller's out-parameter
      if (attachment.out_handle) {
        *attachment.out_handle = handle;
      }
    }

    // Apply additional reads/writes
    applyDependencies(task);

    return task;
  }

}  // namespace ren
