#include <ren/renderer/graph/Schedule.h>
#include <ren/renderer/graph/RenderGraph.h>

namespace ren {

  namespace {

    // A barrier task for transitioning a resource's access state.
    // This task is created and owned by RenderSchedule during compilation.
    // It represents a synchronization point where a resource transitions from one
    // GraphAccess state to another (e.g., from RenderTarget to ShaderRead).
    // Each resource type handles its own barrier emission via emitBarrier().
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
        // Delegate to the resource to emit its specific barrier type
        auto resource = ctx.graph.get<ren::GraphResource>(m_resource);
        resource->emitBarrier(ctx, m_fromAccess, m_toAccess);
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

    ren::println("Schedule validation passed: {} tasks across {} levels", tasks.size(),
                 levelTasks.size());
    return true;
  }

}  // namespace ren