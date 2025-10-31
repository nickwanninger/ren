#include <ren/renderer/RenderGraph.h>

#include <imgui/imgui.h>
#include <ImGuizmo/ImGuizmo.h>
#include <ImGuizmo/GraphEditor.h>
#include <unordered_map>
#include <queue>
#include <chrono>

namespace ren {

  // Implementation of RenderGraph would go here


  //
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


  std::string GraphOperand::toString(void) const {
    return fmt::format("{} %{}", access, valueHandle);
    // return fmt::format("%{}", valueHandle);
  }




  RenderTask &RenderTask::read(GraphHandle handle, GraphAccess access) {
    m_graph.addRead(*this, handle, access);
    GraphResourceType resourceType = m_graph.getResourceType(handle);
    m_operands.push_back(GraphOperand(handle, access, resourceType));
    return *this;
  }

  RenderTask &RenderTask::write(GraphHandle handle, GraphAccess access) {
    m_graph.addWrite(*this, handle, access);
    m_results.push_back(handle);
    return *this;
  }


  std::string RenderTask::toString(void) const {
    // print like an llvm SSA instruction
    std::stringstream ss;

    // Results (writes)
    int ind = 0;
    for (const auto &resultHandle : getResults()) {
      if (ind++ > 0) ss << ", ";
      ss << fmt::format("%{}", resultHandle);
    }

    ss << " ← " << name() << "(";

    // Operands (reads)
    ind = 0;
    for (const auto &operand : getOperands()) {
      if (ind++ > 0) ss << ", ";
      ss << operand.toString();
    }

    ss << ")";

    return ss.str();
  }


  RenderGraph::RenderGraph() {}


  void RenderGraph::prepare(glm::uvec2 swapchainSize) {
    this->swapchainSize = swapchainSize;
    return;
    fmt::println("Preparing render graph with swapchain size {}x{}", swapchainSize.x,
                 swapchainSize.y);

    // Here we would allocate resources based on the resourceTable and their specs.
    for (const auto &[handle, resource] : resourceTable) {
      if (resource.type == GraphResourceType::Image) {
        fmt::println(" - Preparing image resource '{}' (handle {})", resource.name, handle);
        // Allocate image here based on its spec.
      }
    }
  }

  GraphHandle RenderGraph::createImage(const std::string_view &name, const GraphImageSpec &spec,
                                       GraphAccess initialAccess) {
    GraphHandle handle = nextHandle++;

    ResourceEntry entry;
    entry.name = name;
    entry.type = GraphResourceType::Image;
    entry.initialAccess = initialAccess;

    bool relativeScale = not(spec.scale.x == 0.0f and spec.scale.y == 0.0f);

    if (relativeScale) {
      // fmt::println("Creating image '{}' with swapchain-relative scale ({}, {})", name,
      // spec.scale.x,
      //              spec.scale.y);
    } else {
      // fmt::println("Creating image '{}' with size {}x{}", name, spec.width, spec.height);
    }

    // Store the entry in the resource table so the name persists
    resourceTable[handle] = entry;

    // Here, we would allocate the actual GPU resource based on its spec.

    return handle;
  }

  GraphHandle RenderGraph::addRead(RenderTask &task, GraphHandle handle, GraphAccess access) {
    // fmt::println("Task '{}' reads resource {} with access {}", task.name(), handle, access);

    auto &entry = resourceTable[handle];
    entry.users.insert(&task);
    return handle;
  }

  GraphHandle RenderGraph::addWrite(RenderTask &task, GraphHandle handle, GraphAccess access) {
    // fmt::println("Task '{}' writes resource {} with access {}", task.name(), handle, access);

    auto &entry = resourceTable[handle];
    if (entry.definingTask != nullptr) {
      throw std::runtime_error(fmt::format("Warning: Resource {} already has a defining task '{}'",
                                           handle, entry.definingTask->name()));
    }

    entry.definingTask = &task;
    entry.writeAccess = access;  // Track what access state this resource is written to
    return handle;
  }


  // Compute data dependencies based on operand/result relationships
  // For each task, its dependencies are the tasks that write to resources it reads.
  void RenderGraph::computeDependencies(void) {
    // Clear existing dependencies
    for (const auto &entry : tasks) {
      entry.task->dependencies.clear();
    }

    // Build dependencies from resource flow:
    // For each resource, all readers depend on the writer
    for (const auto &[handle, resource] : resourceTable) {
      if (resource.definingTask != nullptr) {
        for (auto *reader : resource.users) {
          reader->dependencies.insert(resource.definingTask);
        }
      }
    }
  }

  RenderTask *RenderGraph::getDefiningTask(GraphHandle resourceHandle) {
    auto it = resourceTable.find(resourceHandle);
    if (it == resourceTable.end()) {
      throw std::runtime_error(fmt::format("Invalid resource handle: {}", resourceHandle));
    }

    auto *definingTask = it->second.definingTask;
    if (definingTask == nullptr) {
      throw std::runtime_error(
          fmt::format("Resource {} ('{}') has no defining task - it was never written to",
                      resourceHandle, it->second.name));
    }

    return definingTask;
  }

  // Topological sort: DFS to order tasks respecting dependencies
  void RenderGraph::topologicalSort(RenderTask *task, std::vector<RenderTask *> &outOrder,
                                    std::unordered_set<RenderTask *> &visited) {
    if (task == nullptr || visited.count(task)) return;
    visited.insert(task);

    // Visit all dependencies first (pre-order traversal)
    for (auto *dep : task->dependencies) {
      topologicalSort(dep, outOrder, visited);
    }

    // Add this task after its dependencies
    outOrder.push_back(task);
  }

  RenderSchedule RenderGraph::compile(GraphHandle goalResource) {
    // Step 1: Find the task that produces the goal resource
    RenderTask *goalTask = getDefiningTask(goalResource);

    // Step 2: Compute dependencies from operand/result relationships
    computeDependencies();

    // Step 3: Compute scheduling levels via DFS to determine parallelism
    // (depth in the dependency DAG from goal to leaves)
    std::unordered_map<RenderTask *, int> taskLevels;
    std::function<int(RenderTask *)> computeLevels = [&](RenderTask *task) -> int {
      if (taskLevels.count(task)) return taskLevels[task];

      int maxDepLevel = 0;
      for (auto *dep : task->dependencies) {
        maxDepLevel = std::max(maxDepLevel, computeLevels(dep));
      }

      int level = maxDepLevel + 1;
      taskLevels[task] = level;
      return level;
    };
    computeLevels(goalTask);

    // Step 4: Topologically sort tasks starting from goal
    std::vector<RenderTask *> orderedTasks;
    std::unordered_set<RenderTask *> visited;
    topologicalSort(goalTask, orderedTasks, visited);

    // Step 5: Sort tasks by level (ascending) so all tasks of the same level are grouped together.
    // This allows executing all tasks of a level before moving to the next level.
    std::sort(orderedTasks.begin(), orderedTasks.end(),
              [&taskLevels](RenderTask *a, RenderTask *b) {
                int levelA = taskLevels.count(a) ? taskLevels[a] : 0;
                int levelB = taskLevels.count(b) ? taskLevels[b] : 0;
                return levelA < levelB;
              });

    // Step 6: Create schedule with level-sorted tasks, insert barriers, and assign levels
    RenderSchedule schedule(*this);

    // Track current access state of each resource (initialized from initialAccess)
    std::unordered_map<GraphHandle, GraphAccess> resourceStates;
    for (const auto &[handle, entry] : resourceTable) {
      resourceStates[handle] = entry.initialAccess;
    }

    // Process tasks in order, inserting barriers as needed
    for (auto *task : orderedTasks) {
      // Check operands (reads): insert barriers if access state needs to change
      for (const auto &operand : task->getOperands()) {
        GraphAccess currentState = resourceStates[operand.valueHandle];
        GraphAccess neededState = operand.access;

        if (currentState != neededState) {
          // Insert a barrier to transition the resource
          auto *barrier = schedule.createBarrier(operand.valueHandle, currentState, neededState);
          schedule.addTask(barrier);

          // Update resource state after barrier
          resourceStates[operand.valueHandle] = neededState;
        }
      }

      // Add the actual task
      schedule.addTask(task);

      // Update resource states based on what this task writes (results)
      for (const auto &resultHandle : task->getResults()) {
        // Look up the write access from the resource table
        auto it = resourceTable.find(resultHandle);
        if (it != resourceTable.end()) { resourceStates[resultHandle] = it->second.writeAccess; }
      }
    }

    // Copy task levels into the schedule
    schedule.taskLevels = taskLevels;

    return schedule;
  }

  void RenderGraph::runFor(GraphHandle goalResource) {
    auto it = resourceTable.find(goalResource);
    if (it == resourceTable.end()) {
      throw std::runtime_error(fmt::format("Invalid resource handle for runFor: {}", goalResource));
    }

    auto start = std::chrono::high_resolution_clock::now();
    auto schedule = compile(goalResource);
    schedule.validate();
    auto end = std::chrono::high_resolution_clock::now();

    auto durationNs = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    fmt::println("Compiled render graph schedule in {} ns", durationNs);

    int currentLevel = 0;
    for (const auto &task : schedule.getTasks()) {
      if (schedule.getLevel(task) != currentLevel) {
        currentLevel = schedule.getLevel(task);
        fmt::println("{:04d}:", currentLevel);
      }
      fmt::println("  {}", task->toString());
    }
  }

  void BarrierTask::run(GraphRunContext &ctx) {
    // TODO: Implement barrier synchronization logic
    // This should perform layout transitions and pipeline barriers for the resource
    // from m_fromAccess to m_toAccess
    fmt::println("Barrier: transitioning resource {} from {} to {}", m_resource, m_fromAccess,
                 m_toAccess);
  }

  std::string BarrierTask::toString(void) const {
    // Format as: [BARRIER] %resource: fromAccess → toAccess
    return fmt::format("barrier %{}: {} → {}", m_resource, m_fromAccess, m_toAccess);
  }

  BarrierTask *RenderSchedule::createBarrier(GraphHandle resource, GraphAccess fromAccess,
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
