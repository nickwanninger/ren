#include <ren/renderer/RenderGraph.h>

#include <imgui/imgui.h>
#include <ImGuizmo/ImGuizmo.h>
#include <ImGuizmo/GraphEditor.h>
#include <unordered_map>
#include <queue>

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

    ss << " = " << name() << "(";

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
      fmt::println("Creating image '{}' with swapchain-relative scale ({}, {})", name, spec.scale.x,
                   spec.scale.y);
    } else {
      fmt::println("Creating image '{}' with size {}x{}", name, spec.width, spec.height);
    }

    // Store the entry in the resource table so the name persists
    resourceTable[handle] = entry;

    // Here, we would allocate the actual GPU resource based on its spec.

    return handle;
  }

  GraphHandle RenderGraph::addRead(RenderTask &task, GraphHandle handle, GraphAccess access) {
    fmt::println("Task '{}' reads resource {} with access {}", task.name(), handle, access);

    auto &entry = resourceTable[handle];
    entry.users.insert(&task);
    return handle;
  }

  GraphHandle RenderGraph::addWrite(RenderTask &task, GraphHandle handle, GraphAccess access) {
    fmt::println("Task '{}' writes resource {} with access {}", task.name(), handle, access);

    auto &entry = resourceTable[handle];
    if (entry.definingTask != nullptr) {
      throw std::runtime_error(fmt::format("Warning: Resource {} already has a defining task '{}'",
                                           handle, entry.definingTask->name()));
    }

    entry.definingTask = &task;
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

    fmt::println("Compiling render graph with goal resource {} -> task '{}'", goalResource,
                 goalTask->name());

    // Step 2: Compute dependencies from operand/result relationships
    computeDependencies();

    // Step 3: Topologically sort tasks starting from goal
    std::vector<RenderTask *> orderedTasks;
    std::unordered_set<RenderTask *> visited;
    topologicalSort(goalTask, orderedTasks, visited);

    // Step 4: Create schedule with ordered tasks
    RenderSchedule schedule(*this);
    for (auto *task : orderedTasks) {
      schedule.addTask(task);
    }

    return schedule;
  }

  void RenderGraph::runFor(GraphHandle goalResource) {
    auto it = resourceTable.find(goalResource);
    if (it == resourceTable.end()) {
      throw std::runtime_error(fmt::format("Invalid resource handle for runFor: {}", goalResource));
    }

    fmt::println("Running render graph for goal resource {} ('{}')", goalResource, it->second.name);

    auto schedule = compile(goalResource);

    fmt::println("Schedule for goal resource '{}' ({} tasks):", it->second.name,
                 schedule.getTasks().size());
    for (const auto &task : schedule.getTasks()) {
      fmt::println("  {}", task->toString());
    }

    // Print dependencies for debug
    fmt::println("digraph {{");

    auto repr = [](RenderTask *task) { return fmt::format("{}.{}", task->name(), (void *)task); };

    for (const auto &entry : tasks) {
      for (auto *dep : entry.task->dependencies) {
        fmt::println("  \"{}\" -> \"{}\";", repr(dep), repr(entry.task.get()));
      }
    }
    fmt::println("}}");
  }


}  // namespace ren
