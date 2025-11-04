#pragma once


#include <ren/renderer/graph/Handle.h>
#include <ren/renderer/graph/Resource.h>

#include <unordered_set>

namespace ren {

  class RenderGraph;  // Forward declaration
  class RenderTask;   // Forward declaration



  /**
   * @struct GraphRunContext
   * @brief Context information passed to each task during execution.
   *
   * This structure provides tasks with access to the render graph and execution context
   * during the @ref RenderTask::run() callback. It allows tasks to query graph state and
   * interact with other graph components without needing direct knowledge of implementation
   * details.
   *
   * @note The context is valid only during the task's run() execution. Do not store
   *       pointers to it for later use.
   */
  struct GraphRunContext {
    ren::RenderGraph &graph;  ///< Reference to the render graph being executed
    ren::RenderTask *task;    ///< Pointer to the task currently being executed (set by the graph)

    /**
     * @brief Constructs a GraphRunContext for the given render graph.
     * @param g Reference to the render graph
     */
    GraphRunContext(ren::RenderGraph &g)
        : graph(g) {}
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

    const std::string &name() const { return m_name; }

    void setName(const std::string &name) { m_name = name; }


    RenderTask &read(GraphHandle handle, GraphAccess access);
    RenderTask &write(GraphHandle handle, GraphAccess access);

    const auto &getOperands(void) const { return m_operands; }
    const auto &getResults(void) const { return m_results; }
    virtual std::string toString(void) const;

   private:
    std::string m_name;    ///< Human-readable name of this task
    RenderGraph &m_graph;  ///< Reference to owning render graph

    std::vector<GraphOperand> m_operands;
    std::vector<GraphHandle> m_results;

   protected:
    friend class RenderGraph;
    friend class RenderSchedule;

    std::unordered_set<RenderTask *> dependencies;
  };




  class RenderPassTask : public RenderTask {
   public:
    //
  };


}  // namespace ren