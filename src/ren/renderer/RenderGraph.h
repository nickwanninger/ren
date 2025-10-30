#pragma once

#include <string>
#include <vector>
#include <map>
#include <ren/types.h>
#include <unordered_set>
#include <ren/core/UUID.h>
#include <ren/renderer/Image.h>

namespace ren {
  class RenderGraph;  // Forward declaration
  class RenderTask;


  // An identifier for a resource in the render graph.
  using GraphHandle = u32;
  static constexpr GraphHandle nullGraphHandle = 0;  // 0 is reserved as null.

  enum class GraphResourceType : u8 {
    Image,   // Constructed with GraphImageSpec/createImage
    Buffer,  // Not yet implemented. For GPU compute.
  };

  // When accessing a resource, what kind of access is performed. This is used
  // to determine layout transitions and pipeline barriers required for the
  // resource between tasks. For example, a resource written as a RenderTarget
  // in one task, and read as a ShaderRead in another will require a layout
  // transition and appropriate pipeline barriers.  This is a simplified model
  // compared to real graphics APIs, and I'll expand it as needed, but basic
  // functionality should be covered.
  enum class GraphAccess : u8 {
    // The resource is written as a render target (color)
    RenderTarget,
    // The resource is written as a depth target
    DepthTarget,

    // The resource is read in a shader
    VertexShaderRead,
    FragmentShaderRead,
    ComputeShaderRead,


    // The resource is written in a compute shader
    // Note: Writing in fragment or vertex shaders is not supported, as vulkan doesn't.
    ComputeShaderWrite,
    // This is here for completeness, but is not handled in the render graph
    // scheduler, as it only supports RAW depdendencies at the moment, where
    // there is one writer, and many readers. The readers are not expected to be
    // ordered when executed, so a WAW or WAR dependency cannot be properly
    // handled.  This would require more complex scheduling (likely involving
    // something like aliased resources).
    ComputeShaderReadWrite,

    // Generic read in all shaders.
    ShaderRead,
  };


  // An operand of a resource in the render graph.
  // This represents a single location where a resource is used as an operand (read), and how it is used.
  // Think about this like an llvm::Use (it's an argument, basically).
  struct GraphOperand {
    GraphHandle valueHandle;
    GraphAccess access;
    GraphResourceType resourceType;

    GraphOperand(GraphHandle h, GraphAccess a, GraphResourceType type)
        : valueHandle(h)
        , access(a)
        , resourceType(type) {}

    std::string toString(void) const;
  };




  // The specification for creating an image in the render graph.
  struct GraphImageSpec {
    // Resolution should come from the swapchain size multiplied by this scale factor.
    // If this value is 0, the size is absolute. (.width, .height)
    glm::vec2 scale = glm::vec2(0.0f);

    // Absolute width/height.
    u32 width = 0;
    u32 height = 0;
  };




  // Context passed to each task when it is run.
  struct GraphRunContext {
    RenderGraph &graph;
    RenderTask *task;  // This is the task being run.

    GraphRunContext(RenderGraph &g)
        : graph(g) {}
  };

  class RenderTask {
   public:
    RenderTask(RenderGraph &graph)
        : m_graph(graph) {}
    virtual ~RenderTask() = default;

    // Run this task in the render graph with a given context.
    // We pass this GraphContext to allow the growth of functionality in the future.
    virtual void run(GraphRunContext &ctx) = 0;

    const std::string &name() const { return m_name; }
    void setName(const std::string &name) { m_name = name; }

    // SSA-style interface: operands (reads) and results (writes)
    RenderTask &read(GraphHandle handle, GraphAccess access);
    RenderTask &write(GraphHandle handle, GraphAccess access);

    // Accessors for operands and results
    const auto &getOperands(void) const { return m_operands; }
    const auto &getResults(void) const { return m_results; }

    std::string toString(void) const;

   private:
    std::string m_name;
    RenderGraph &m_graph;

    // SSA operands - inputs to this instruction (resources being read)
    std::vector<GraphOperand> m_operands;

    // SSA results - outputs from this instruction (resources being written)
    std::vector<GraphHandle> m_results;

   protected:
    friend class RenderGraph;  // Allow RenderGraph to access protected members
    friend class RenderSchedule;

    // Data dependencies: who must run before me.
    // These are automatically derived from operands:
    // For each operand, we depend on the task that wrote it.
    // Updated by RenderGraph after building the graph.
    std::unordered_set<RenderTask *> dependencies;
  };


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


  // A barrier task for transitioning a resource's access state.
  // This task is created and owned by RenderSchedule during compilation.
  // It represents a synchronization point where a resource transitions from one
  // GraphAccess state to another (e.g., from RenderTarget to ShaderRead).
  class BarrierTask : public RenderTask {
   public:
    BarrierTask(RenderGraph &graph, GraphHandle resource, GraphAccess fromAccess,
                GraphAccess toAccess)
        : RenderTask(graph)
        , m_resource(resource)
        , m_fromAccess(fromAccess)
        , m_toAccess(toAccess) {
      setName("barrier");
    }

    void run(GraphRunContext &ctx) override;

    GraphHandle getResource() const { return m_resource; }
    GraphAccess getFromAccess() const { return m_fromAccess; }
    GraphAccess getToAccess() const { return m_toAccess; }

   private:
    GraphHandle m_resource;
    GraphAccess m_fromAccess;
    GraphAccess m_toAccess;
  };



  class RenderSchedule {
   public:
    RenderSchedule(ren::RenderGraph &graph)
        : graph(graph) {};

    inline void addTask(RenderTask *task) { tasks.push_back(task); }

    const std::vector<RenderTask *> &getTasks() const { return tasks; }

    // Get the scheduling level of a task (depth in DFS traversal from goal).
    // Tasks at the same level have no data dependencies and can be executed in parallel.
    // Returns -1 if the task is not in this schedule.
    int getLevel(RenderTask *task) const;

    // Create a barrier task for transitioning a resource between access states.
    // The barrier task is owned by this schedule (stored in syncTasks).
    // Returns a pointer to the barrier task so it can be added to the schedule.
    BarrierTask *createBarrier(GraphHandle resource, GraphAccess fromAccess, GraphAccess toAccess);

    // Validate the schedule: ensure no two tasks at the same level have dependencies on each other.
    // Throws std::runtime_error if validation fails. Returns true if valid.
    bool validate(void) const;

    // dump the schedule as SSA form to stdout for debugging.
    void dump(void);

   private:
    ren::RenderGraph &graph;
    std::vector<RenderTask *> tasks;

    // Scheduling levels: maps each task to its level (depth in DFS from goal).
    // Set during compilation.
    std::unordered_map<RenderTask *, int> taskLevels;

    // a schedule has synchronization tasks that must be run between certain tasks
    // We insert this into the schedule as needed as Tasks, to unify the API for execution.
    // They live here, as they are not user-defined, we need to store them somewhere. They are
    // created during baking.
    std::vector<ref<RenderTask>> syncTasks;

    // Helper: check if taskA has a transitive dependency on taskB
    bool dependsOn(RenderTask *taskA, RenderTask *taskB) const;

    friend class RenderGraph;  // Allow RenderGraph to set task levels during compilation
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


    // prepare the render graph for execution (allocate resources, etc)
    void prepare(glm::uvec2 swapchainSize);


    // Print the render graph as an SSA form to stdout for debugging.
    void dumpSSA(void);

    // Compile the graph into a schedule of tasks to run using topological sort.
    // Starting from the task that writes the goal resource, computes task dependencies
    // from operand/result relationships, then topologically sorts to create a valid execution order.
    // NOTE: this is slow right now. Will look into caching and invalidating.
    RenderSchedule compile(GraphHandle goalResource);


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

      // SSA-style tracking:
      // The task that defines (writes to) this resource
      RenderTask *definingTask = nullptr;

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
    GraphHandle nextHandle = ren::nullGraphHandle + 1;  // Start at the first valid handle.

    glm::uvec2 swapchainSize;
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


template <>
struct fmt::formatter<ren::GraphAccess> : fmt::formatter<std::string_view> {
  static const char *toString(ren::GraphAccess access) {
#define CASE_ENUM_TO_STRING(e) \
  case ren::GraphAccess::e: return #e;
    switch (access) {
      CASE_ENUM_TO_STRING(RenderTarget);
      CASE_ENUM_TO_STRING(DepthTarget);
      CASE_ENUM_TO_STRING(FragmentShaderRead);
      CASE_ENUM_TO_STRING(VertexShaderRead);
      CASE_ENUM_TO_STRING(ComputeShaderRead);
      CASE_ENUM_TO_STRING(ComputeShaderWrite);
      CASE_ENUM_TO_STRING(ComputeShaderReadWrite);
      CASE_ENUM_TO_STRING(ShaderRead);
    }
#undef CASE_ENUM_TO_STRING
  }

  auto format(ren::GraphAccess access, fmt::format_context &ctx) const {
    return fmt::formatter<std::string_view>::format(toString(access), ctx);
  }
};


template <>
struct fmt::formatter<ren::GraphResourceType> : fmt::formatter<std::string_view> {
  static const char *toString(ren::GraphResourceType access) {
#define CASE_ENUM_TO_STRING(e) \
  case ren::GraphResourceType::e: return #e;
    switch (access) {
      CASE_ENUM_TO_STRING(Image);
      CASE_ENUM_TO_STRING(Buffer);
    }
#undef CASE_ENUM_TO_STRING
  }

  auto format(ren::GraphResourceType access, fmt::format_context &ctx) const {
    return fmt::formatter<std::string_view>::format(toString(access), ctx);
  }
};