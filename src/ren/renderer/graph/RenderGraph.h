#pragma once

#include <vector>
#include <ren/types.h>
#include <unordered_set>
#include <ren/core/UUID.h>
#include <ren/renderer/Image.h>


#include <ren/renderer/graph/Handle.h>
#include <ren/renderer/graph/RunContext.h>
#include <ren/renderer/graph/Resource.h>
#include <ren/renderer/graph/ImageResource.h>
#include <ren/renderer/graph/Task.h>
#include <ren/renderer/graph/Schedule.h>

namespace ren {
  class RenderGraph;  // Forward declaration
  class RenderTask;


  using RenderTaskLambda = std::function<void(GraphRunContext &ctx)>;


  class RenderGraph {
   public:
    RenderGraph();

    // No copy, no move
    RenderGraph(const RenderGraph &) = delete;
    RenderGraph &operator=(const RenderGraph &) = delete;
    RenderGraph(RenderGraph &&) = delete;
    RenderGraph &operator=(RenderGraph &&) = delete;

    // Add a task of a given type to the render graph
    template <typename T, typename... Args>
    T &addTask(const char *name, Args &&...args);
    // Add a task as a lambda function
    RenderTask &addTask(const char *name, RenderTaskLambda &&func);


    GraphHandle createImage(const std::string_view &name, const GraphImageSpec &spec,
                            GraphAccess initialAccess);
    // Get the Image resource for a given handle.
    ren::ImageRef getImage(GraphHandle handle) const {
      return get<ren::ImageResource>(handle)->image;
    }

    auto getResource(GraphHandle handle) const { return resourceTable.at(handle); }


    // Declare a read of a resource by a task (used internally)
    GraphHandle addRead(RenderTask &task, GraphHandle handle, GraphAccess access);
    // Declare a write of a resource by a task (used internally)
    GraphHandle addWrite(RenderTask &task, GraphHandle handle, GraphAccess access);
    using Schedule = std::vector<RenderTask *>;


    // Create a schedule of tasks to run such that a goal resource is produced.
    // For example, if you want the swapchain to be written, pass the swapchain resource handle.
    // Internally finds the task that writes this resource and schedules all dependencies.
    void runFor(GraphHandle goalResource, class Renderer &renderer);




    // Print the render graph as an SSA form to stdout for debugging.
    void dumpSSA(void);
    // Inspect the render graph (in imgui.)
    void inspect(void);

    // Start a new frame with the given swapchain image.
    // This will take the swapchain image size, and use it to update any image resources
    // which are defined relative to the swapchain size.
    // This returns true if any resources were reallocated, false otherwise.
    bool startFrame(glm::uvec2 renderSize);

    /**
     * @brief Get the current swapchain size.
     * @return The dimensions of the swapchain in pixels
     */
    glm::uvec2 getSwapchainSize() const { return swapchainSize; }

    // Compile the graph into a schedule of tasks to run using topological sort.
    // Starting from the task that writes the goal resource, computes task dependencies
    // from operand/result relationships, then topologically sorts to create a valid execution
    // order. NOTE: this is slow right now. Will look into caching and invalidating.
    RenderSchedule compile(GraphHandle goalResource);


    /**
     * @brief Get a resource of a specific type from the graph.
     * @tparam T The resource type to retrieve (e.g., ImageResource, BufferResource, or
     * GraphResource)
     * @param handle The resource handle
     * @return A ref to the resource cast to type T
     * @throws std::runtime_error if handle is invalid or type doesn't match
     */
    template <typename T>
    ref<T> get(GraphHandle handle) const {
      auto it = resourceTable.find(handle);
      if (it == resourceTable.end()) {
        throw std::runtime_error(fmt::format("Invalid graph handle: {}", handle));
      }

      auto casted = std::dynamic_pointer_cast<T>(it->second);
      if (!casted) {
        throw std::runtime_error(fmt::format(
            "Resource {} has type {} but requested type does not match", handle, it->second->type));
      }

      return casted;
    }

    inline GraphResourceType getResourceType(GraphHandle handle) const {
      auto it = resourceTable.find(handle);
      if (it == resourceTable.end()) { throw std::runtime_error("Invalid graph handle"); }
      return it->second->type;
    }

   private:
    // Helper: compute dependencies for all tasks based on operand/result relationships
    void computeDependencies(void);

    // Helper: get the task that defines (writes to) a resource
    RenderTask *getDefiningTask(GraphHandle resourceHandle);

    // Helper: perform topological sort to produce a valid execution order
    void topologicalSort(RenderTask *task, std::vector<RenderTask *> &outOrder,
                         std::unordered_set<RenderTask *> &visited);

    std::unordered_map<GraphHandle, ref<GraphResource>> resourceTable;
    std::vector<ref<RenderTask>> tasks;
    GraphHandle nextHandle = ren::userGraphHandleStart;

    glm::uvec2 swapchainSize;
    ref<ren::Image> swapchainImage = nullptr;


    // Total time spent compiling the graph across all runs.
    u64 compileTimeUs = 0;
    // Total time spent running the graph across all runs.
    u64 totalRuntimeUs = 0;
    // Total number of runs.
    u64 numRuns = 0;
  };


  template <typename T, typename... Args>
  inline T &RenderGraph::addTask(const char *name, Args &&...args) {
    static_assert(
        std::is_base_of<RenderTask, T>::value,
        "when adding a task to the render graph, it must be derived from ren::RenderTask");
    auto task = makeRef<T>(*this, std::forward<Args>(args)...);
    task->setName(name);
    tasks.push_back(task);
    return *task;
  }



  // Construct a task from a lambda function
  class LambdaRenderTask : public RenderTask {
   public:
    using Callback = RenderTaskLambda;

    LambdaRenderTask(RenderGraph &graph, Callback func)
        : RenderTask(graph)
        , m_func(std::move(func)) {}

    void run(GraphRunContext &ctx) override { m_func(ctx); }

   private:
    Callback m_func;
  };


  inline RenderTask &RenderGraph::addTask(const char *name, RenderTaskLambda &&func) {
    return addTask<LambdaRenderTask>(name, std::move(func));
  }

}  // namespace ren
