#include <ren/renderer/graph/Schedule.h>


namespace ren {

  namespace {

    struct ImageBarrierInfo {
      VkPipelineStageFlags stage;
      VkAccessFlags access;
      VkImageLayout layout;
    };


    static ImageBarrierInfo getImageBarrierInfoForAccess(GraphAccess access) {
      ImageBarrierInfo info;

      switch (access) {
        case GraphAccess::RenderTarget:
          info.stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
          info.access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
          info.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
          break;
        case GraphAccess::DepthTarget:
          info.stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
          info.access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
          info.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
          break;
        case GraphAccess::FragmentShaderRead:
          info.stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
          info.access = VK_ACCESS_SHADER_READ_BIT;
          info.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
          break;
        case GraphAccess::VertexShaderRead:
          info.stage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
          info.access = VK_ACCESS_SHADER_READ_BIT;
          info.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
          break;
        case GraphAccess::ComputeShaderRead:
          info.stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
          info.access = VK_ACCESS_SHADER_READ_BIT;
          info.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
          break;
        case GraphAccess::ComputeShaderWrite:
          info.stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
          info.access = VK_ACCESS_SHADER_WRITE_BIT;
          info.layout = VK_IMAGE_LAYOUT_GENERAL;
          break;
        case GraphAccess::ComputeShaderReadWrite:
          info.stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
          info.access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
          info.layout = VK_IMAGE_LAYOUT_GENERAL;
          break;

        case GraphAccess::ShaderRead:
          info.stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
          info.access = VK_ACCESS_SHADER_READ_BIT;
          info.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
          break;
      }


      return info;
    }


    // A barrier task for transitioning a resource's access state.
    // This task is created and owned by RenderSchedule during compilation.
    // It represents a synchronization point where a resource transitions from one
    // GraphAccess state to another (e.g., from RenderTarget to ShaderRead).
    // This is technically private to RenderSchedule.
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

      void run(GraphRunContext &ctx) override {
        // TODO: hand this off to the resource directly.
        fmt::println("Barrier: transitioning resource {} from {} to {}", m_resource, m_fromAccess,
                     m_toAccess);
      }
      std::string toString(void) const override {
        return fmt::format("barrier %{}: {} → {}", m_resource, m_fromAccess, m_toAccess);
      }

      GraphHandle getResource() const { return m_resource; }
      GraphAccess getFromAccess() const { return m_fromAccess; }
      GraphAccess getToAccess() const { return m_toAccess; }

     private:
      GraphHandle m_resource;
      GraphAccess m_fromAccess;
      GraphAccess m_toAccess;
    };

  }  // namespace


  RenderTask *RenderSchedule::createBarrier(GraphHandle resource, GraphAccess fromAccess,
                                            GraphAccess toAccess) {
    auto barrier = std::make_shared<BarrierTask>(graph, resource, fromAccess, toAccess);
    syncTasks.push_back(barrier);
    return barrier.get();
  }

  int RenderSchedule::getLevel(RenderTask *task) const {
    auto it = taskLevels.find(task);
    if (it == taskLevels.end()) {
      return -1;  // Task not in this schedule
    }
    return it->second;
  }

  // Helper: check if taskA has a transitive dependency on taskB
  bool RenderSchedule::dependsOn(RenderTask *taskA, RenderTask *taskB) const {
    std::unordered_set<RenderTask *> visited;
    std::function<bool(RenderTask *)> hasPath = [&](RenderTask *task) -> bool {
      if (task == taskB) return true;
      if (visited.count(task)) return false;
      visited.insert(task);

      for (auto *dep : task->dependencies) {
        if (hasPath(dep)) return true;
      }
      return false;
    };

    return hasPath(taskA);
  }

  bool RenderSchedule::validate(void) const {
    // Group tasks by level
    std::unordered_map<int, std::vector<RenderTask *>> levelTasks;
    for (const auto &task : tasks) {
      int level = getLevel(task);
      if (level >= 0) {  // Skip tasks not in taskLevels
        levelTasks[level].push_back(task);
      }
    }

    // For each level, verify no two tasks have dependencies on each other
    for (const auto &[level, tasksAtLevel] : levelTasks) {
      for (size_t i = 0; i < tasksAtLevel.size(); ++i) {
        for (size_t j = i + 1; j < tasksAtLevel.size(); ++j) {
          RenderTask *taskA = tasksAtLevel[i];
          RenderTask *taskB = tasksAtLevel[j];

          // Check if A depends on B
          if (dependsOn(taskA, taskB)) {
            throw std::runtime_error(fmt::format(
                "Schedule validation failed: task '{}' (level {}) depends on task '{}' at the same "
                "level",
                taskA->name(), level, taskB->name()));
          }

          // Check if B depends on A
          if (dependsOn(taskB, taskA)) {
            throw std::runtime_error(fmt::format(
                "Schedule validation failed: task '{}' (level {}) depends on task '{}' at the same "
                "level",
                taskB->name(), level, taskA->name()));
          }
        }
      }
    }

    fmt::println("Schedule validation passed: {} tasks across {} levels", tasks.size(),
                 levelTasks.size());
    return true;
  }

}  // namespace ren