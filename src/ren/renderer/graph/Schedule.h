#pragma once
#include <ren/renderer/graph/Task.h>
#include <variant>

namespace ren {


  // Run one task.
  struct ScheduledRenderTask {
    ref<RenderTask> task;
  };


  // Some resource is transitioning from one access type to another. This is a
  // barrier that is inserted into the schedule to ensure that the resource is
  // in the correct state before the next task uses it.
  struct ScheduledResourceTransition {
    ref<GraphStorage> storage;
    std::vector<GraphAccess> from;  // The last write, or the reads since the last write. Empty for new storage.
    GraphAccess to;
  };


  // A render schedule is a list of tasks and resource transitions that are to
  // execute in order. We store them in a vector of variants so we can have the
  // one list. It holds strong references to the tasks, so they aren't destroyed
  // while the schedule is executing. The graph makes a new schedule each frame.
  struct RenderSchedule {
    // The selected tasks in dependency order, from RenderGraph::compile().
    std::vector<ref<RenderTask>> tasks;

    // The tasks with the barriers between them, from RenderGraph::compileTransitions().
    std::vector<std::variant<ScheduledRenderTask, ScheduledResourceTransition>> operations;
  };
}  // namespace ren
