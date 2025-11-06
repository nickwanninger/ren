#pragma once

#include <ren/renderer/graph/Handle.h>
#include <ren/renderer/graph/Resource.h>
#include <ren/renderer/graph/Task.h>



namespace ren {



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
    RenderTask *createBarrier(GraphHandle resource, GraphAccess fromAccess, GraphAccess toAccess);

    // Validate the schedule: ensure no two tasks at the same level have dependencies on each other.
    // Throws std::runtime_error if validation fails. Returns true if valid.
    bool validate(void) const;

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
}  // namespace ren