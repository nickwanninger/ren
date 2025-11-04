#pragma once

#include <string>
#include <vector>
#include <map>
#include <ren/types.h>
#include <unordered_set>
#include <ren/core/UUID.h>
#include <ren/renderer/Image.h>


#include <ren/renderer/graph/Handle.h>
#include <ren/renderer/graph/Resource.h>
#include <ren/renderer/graph/Task.h>
#include <ren/renderer/graph/Schedule.h>

namespace ren {
  class RenderGraph;  // Forward declaration
  class RenderTask;


  // Construct a task from a lambda function
  class LambdaRenderTask : public RenderTask {
   public:
    using Callback = std::function<void(GraphRunContext &ctx)>;

    LambdaRenderTask(RenderGraph &graph, Callback func)
        : RenderTask(graph)
        , m_func(std::move(func)) {}

    void run(GraphRunContext &ctx) override { m_func(ctx); }

   private:
    Callback m_func;
  };


  class RenderGraph {
   public:
    RenderGraph();
    // Add a task of a given type to the render graph
    template <typename T, typename... Args>
    T &addTask(const char *name, Args &&...args);
    // Add a task as a lambda function
    RenderTask &addTask(const char *name, LambdaRenderTask::Callback &&func);


    GraphHandle createImage(const std::string_view &name, const GraphImageSpec &spec,
                            GraphAccess initialAccess);

    // Declare a read of a resource by a task (used internally)
    GraphHandle addRead(RenderTask &task, GraphHandle handle, GraphAccess access);
    // Declare a write of a resource by a task (used internally)
    GraphHandle addWrite(RenderTask &task, GraphHandle handle, GraphAccess access);
    using Schedule = std::vector<RenderTask *>;


    // Create a schedule of tasks to run such that a goal resource is produced.
    // For example, if you want the swapchain to be written, pass the swapchain resource handle.
    // Internally finds the task that writes this resource and schedules all dependencies.
    void runFor(GraphHandle goalResource);




    // Print the render graph as an SSA form to stdout for debugging.
    void dumpSSA(void);
    // Inspect the render graph (in imgui.)
    void inspect(void);

    // Start a new frame with the given swapchain image.
    // This will take the swapchain image size, and use it to update any image resources
    // which are defined relative to the swapchain size.
    // This returns true if any resources were reallocated, false otherwise.
    bool startFrame(ref<ren::Image> image);

    // Compile the graph into a schedule of tasks to run using topological sort.
    // Starting from the task that writes the goal resource, computes task dependencies
    // from operand/result relationships, then topologically sorts to create a valid execution
    // order. NOTE: this is slow right now. Will look into caching and invalidating.
    RenderSchedule compile(GraphHandle goalResource);

    // Get resources
    // TODO: store resources in the graph as an abstract "GraphResource" type,
    // then have images or buffers be that. Then, you `G.get<T>(handle)` to get
    // a resource of type T.  I would still like to store *anything* in the
    // graph, without needing inheritance from a base class.  Thus, We might
    // just have a `GraphResource` that is a base, then a `TypeGraphResource<T>`
    // that stores a ref to a T.  We'd then have some kind of way to automate
    // the casting and barrier generation as needed for a given T (maybe via
    // traits?)
    template <typename T>
    ref<T> get(GraphHandle handle);


    inline GraphResourceType getResourceType(GraphHandle handle) const {
      auto it = resourceTable.find(handle);
      if (it == resourceTable.end()) { throw std::runtime_error("Invalid graph handle"); }
      return it->second.type;
    }

   private:
    struct TaskEntry {
      std::string name;
      ren::ref<RenderTask> task;
    };

    // A value in the SSA graph - represents a resource
    struct ResourceEntry {
      // The resource name, for debug.
      std::string name;
      GraphResourceType type;
      // The access type it should be in at the start of the frame.
      GraphAccess initialAccess;


      // TODO: move this out to a Resource subclass for all the types.
      ref<Image> image = nullptr;
      GraphImageSpec imageSpec;
      // ------------------

      // SSA-style tracking:
      // The task that defines (writes to) this resource
      RenderTask *definingTask = nullptr;
      // The access type this resource is written with (what state it's in after the write)
      GraphAccess writeAccess = GraphAccess::ShaderRead;  // Default; set by addWrite

      // Tasks that use this resource as an operand (read it)
      std::unordered_set<RenderTask *> users;
    };

    struct ImageResource {
      ImageResource(const GraphImageSpec &spec)
          : spec(spec) {}

      GraphImageSpec spec;

      ren::ImageRef image;
    };

    // Helper: compute dependencies for all tasks based on operand/result relationships
    void computeDependencies(void);

    // Helper: get the task that defines (writes to) a resource
    RenderTask *getDefiningTask(GraphHandle resourceHandle);

    // Helper: perform topological sort to produce a valid execution order
    void topologicalSort(RenderTask *task, std::vector<RenderTask *> &outOrder,
                         std::unordered_set<RenderTask *> &visited);

    std::unordered_map<GraphHandle, ResourceEntry> resourceTable;
    std::vector<TaskEntry> tasks;
    GraphHandle nextHandle = ren::userGraphHandleStart;

    glm::uvec2 swapchainSize;
    ref<ren::Image> swapchainImage = nullptr;
  };


  template <typename T, typename... Args>
  inline T &RenderGraph::addTask(const char *name, Args &&...args) {
    static_assert(
        std::is_base_of<RenderTask, T>::value,
        "when adding a task to the render graph, it must be derived from ren::RenderTask");
    auto task = std::make_shared<T>(*this, std::forward<Args>(args)...);
    task->setName(name);
    tasks.push_back({name, task});
    return *task.get();
  }


  inline RenderTask &RenderGraph::addTask(const char *name, LambdaRenderTask::Callback &&func) {
    return addTask<LambdaRenderTask>(name, std::move(func));
  }

}  // namespace ren


