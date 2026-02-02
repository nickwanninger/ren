#pragma once

#include <ren/renderer/graph/Handle.h>
#include <ren/renderer/graph/Resource.h>
#include <ren/renderer/graph/RunContext.h>

#include <unordered_set>

namespace ren {

  class RenderGraph;  // Forward declaration
  class RenderTask;   // Forward declaration

  struct GraphHandleUsage {
    GraphHandle handle;
    GraphAccess access;

    GraphHandleUsage(GraphHandle h, GraphAccess a)
        : handle(h)
        , access(a) {}
  };


  /**
   * @class RenderTask
   * @brief Base class for tasks that perform rendering operations within a render graph.
   *
   * RenderTask implements the core abstraction for SSA-style dataflow in the render graph system.
   * Tasks declare their inputs (operands) and outputs (results), which allows the graph to
   * automatically derive data dependencies and schedule tasks in proper order.
   *
   * @note Tasks are typically created via @ref RenderGraph::addTask<>() or
   *       @ref RenderGraph::addTask(const char*, Callback&&). Direct instantiation is possible
   *       but not recommended as the graph maintains ownership.
   *
   * @see RenderGraph
   * @see GraphRunContext
   */
  class RenderTask {
   public:
    RenderTask(RenderGraph &graph)
        : m_graph(graph) {}

    /// Virtual destructor for safe polymorphic deletion
    virtual ~RenderTask() = default;


    /**
     * @brief Executes the task's rendering operations.
     *
     * This is the main work function of the task. It is called during the frame execution
     * phase, after all dependencies have completed and resources are prepared. The task should
     * perform its actual rendering work (bind pipelines, issue draw calls, compute dispatches,
     * etc.)
     *
     * @param ctx Execution context containing graph reference and task information. Valid only
     *            during this function call.
     */
    virtual void run(GraphRunContext &ctx) = 0;


    /**
     * @brief Prepares the task for execution after graph resources are allocated.
     */
    virtual void prepare() {}

    /**
     * @brief Cleans up any prepared state before resource deallocation.
     */
    virtual void unprepare() {}

    /**
     * @brief Inspect the task in the imgui-based inspector.
     */
    virtual void inspect(void) {}

    const std::string &name() const { return m_name; }

    void setName(const std::string &name) { m_name = name; }


    RenderTask &read(GraphHandle handle, GraphAccess access);
    RenderTask &write(GraphHandle handle, GraphAccess access);

    const auto &getOperands(void) const { return m_operands; }
    const auto &getResults(void) const { return m_results; }
    virtual std::string toString(void) const;


    RenderGraph &graph(void) const { return m_graph; }



    /**
     * @brief Executes the task within the given context. This method is called
     * by the render graph schedule.
     */
    void execute(GraphRunContext &ctx);

    inline float averageTimeNs(void) {
      if (numExecutions == 0) {
        return 0.0f;
      }
      return static_cast<float>(totalTimeNs / static_cast<double>(numExecutions));
    }

   private:
    std::string m_name;    ///< Human-readable name of this task
    RenderGraph &m_graph;  ///< Reference to owning render graph

    std::vector<GraphOperand> m_operands;
    std::vector<GraphHandleUsage> m_results;

    int version = 1;  // Every time the task is re-prepared, this goes up

    double totalTimeNs = 0.0;
    u64 numExecutions = 0;

   protected:
    friend class RenderGraph;
    friend class RenderSchedule;

    std::unordered_set<RenderTask *> dependencies;
  };



  inline void RenderTask::execute(GraphRunContext &ctx) {
    REN_PROFILE_SCOPE(this->name().c_str());
    auto start = std::chrono::high_resolution_clock::now();
    run(ctx);
    auto end = std::chrono::high_resolution_clock::now();
    float durationNs = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    totalTimeNs += durationNs;
    numExecutions++;
  }


  // This is a builder class for constructing tasks in a clean declarative way.
  // It allows you to configure things like read/write resources, and then
  // finalize it and construct a task with a given execution function (like a
  // lambda).  For example, a RenderTask can be constructed by setting up
  // reads/writes and then calling render().  A simple compute task can be
  // constructed with execute() and manually binding resources.
  class TaskBuilder {
   public:
    TaskBuilder(RenderGraph &graph, std::string name);

    // Move-only (prevent copying and reuse)
    TaskBuilder(const TaskBuilder &) = delete;
    TaskBuilder &operator=(const TaskBuilder &) = delete;
    TaskBuilder(TaskBuilder &&) = default;
    TaskBuilder &operator=(TaskBuilder &&) = delete;

    // Fluent API for dependencies
    TaskBuilder &reads(GraphHandle handle, GraphAccess access);
    TaskBuilder &writes(GraphHandle handle, GraphAccess access);

    // Convenience overloads
    TaskBuilder &reads(GraphHandleUsage resource);
    TaskBuilder &writes(GraphHandleUsage resource);

    // Attachment creation (for render passes)
    TaskBuilder &createColorAttachment(const std::string_view &name, const GraphImageSpec &spec, GraphHandle &out_handle);

    TaskBuilder &createDepthAttachment(const std::string_view &name, const GraphImageSpec &spec, GraphHandle &out_handle);

    // Finalizers (consume builder)
    RenderTask &execute(std::function<void(GraphRunContext &)> func);
    RenderTask &render(std::function<void(GraphRenderPassContext &)> func);

   private:
    struct AttachmentSpec {
      std::string name;
      GraphImageSpec spec;
      GraphHandle *out_handle;
      bool is_depth;
    };

    RenderGraph *m_graph;
    std::string m_name;
    std::vector<GraphHandleUsage> m_reads;
    std::vector<GraphHandleUsage> m_writes;
    std::vector<AttachmentSpec> m_attachments;
    bool m_finalized = false;

    void applyDependencies(RenderTask &task);
  };




}  // namespace ren