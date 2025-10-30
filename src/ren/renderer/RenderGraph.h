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

  // When accessing a resource, what kind of access is performed.
  enum class GraphAccess : u8 {
    // The resource is written as a render target (color)
    RenderTarget,
    // The resource is written as a depth target
    DepthTarget,

    // The resource is read in a shader
    VertexShaderRead,
    FragmentShaderRead,
    // Generic read in a shader (vertex|fragment|compute)
    ShaderRead,
  };


  enum class GraphResourceType : u8 {
    Image,   // Constructed with GraphImageSpec/createImage
    Buffer,  // Not yet implemented. For GPU compute.
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

    RenderTask &read(GraphHandle handle, GraphAccess access);
    RenderTask &write(GraphHandle handle, GraphAccess access);

   private:
    std::string m_name;
    RenderGraph &m_graph;


   protected:
    friend class RenderGraph;  // Allow RenderGraph to access read/write protected

    // Who must run before me (writes to resources I read)
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

    GraphHandle addRead(RenderTask &task, GraphHandle handle, GraphAccess access);
    GraphHandle addWrite(RenderTask &task, GraphHandle handle, GraphAccess access);


    // create a schedule of tasks to run, using one task as a goal
    // For example, if the goal is to present to the swapchain, we would pass in the "blit" task,
    // and schedule according to dependencies.
    void runFor(RenderTask &goalTask);


    // prepare the render graph for execution (allocate resources, etc)
    void prepare(glm::uvec2 swapchainSize);

   private:
    struct TaskEntry {
      std::string name;
      ren::ref<RenderTask> task;
    };


    struct ResourceEntry {
      // The resource name, for debug.
      std::string name;
      GraphResourceType type;
      // The access type it should be in at the start of the frame.
      GraphAccess initialAccess;

      RenderTask *writer;
      std::unordered_set<RenderTask *> readers;
    };


    struct ImageResource {
      ImageResource(const GraphImageSpec &spec)
          : spec(spec) {}

      GraphImageSpec spec;

      ren::ImageRef image;
    };


    std::unordered_map<GraphHandle, ResourceEntry> resourceTable;
    std::vector<TaskEntry> tasks;
    GraphHandle nextHandle = 1;

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
      CASE_ENUM_TO_STRING(ShaderRead);
    }
  }

  auto format(ren::GraphAccess access, fmt::format_context &ctx) const {
    return fmt::formatter<std::string_view>::format(toString(access), ctx);
  }
};
