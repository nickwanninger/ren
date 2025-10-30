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


  std::string GraphUse::toString(void) const {
    auto type = graph.getResourceType(handle);
    // return fmt::format("{}.{} %{}", type, access, handle);
    return fmt::format("%{}", handle);
  }




  RenderTask &RenderTask::read(GraphHandle handle, GraphAccess access) {
    m_graph.addRead(*this, handle, access);
    m_reads.push_back(GraphUse(m_graph, handle, access));
    return *this;
  }

  RenderTask &RenderTask::write(GraphHandle handle, GraphAccess access) {
    m_graph.addWrite(*this, handle, access);
    m_writes.push_back(GraphUse(m_graph, handle, access));
    return *this;
  }


  std::string RenderTask::toString(void) const {
    // print like an llvm SSA instruction
    int ind = 0;

    std::stringstream ss;


    ind = 0;
    for (const auto &use : getWrites()) {
      if (ind++ > 0) ss << ", ";
      ss << use.toString();
    }

    ss << " = " << name() << "(";

    ind = 0;
    for (const auto &use : getReads()) {
      if (ind++ > 0) ss << ", ";
      ss << use.toString();
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

    // Here, we would allocate the resource, but I haven't gotten there yet.

    return handle;
  }

  GraphHandle RenderGraph::addRead(RenderTask &task, GraphHandle handle, GraphAccess access) {
    fmt::println("Task '{}' reads image {} with access {}", task.name(), handle, access);

    auto &entry = resourceTable[handle];
    entry.readers.insert(&task);
    return handle;
  }

  GraphHandle RenderGraph::addWrite(RenderTask &task, GraphHandle handle, GraphAccess access) {
    fmt::println("Task '{}' writes image {} with access {}", task.name(), handle, access);

    auto &entry = resourceTable[handle];
    if (entry.writer != nullptr) {
      throw std::runtime_error(fmt::format("Warning: Image {} already has a writer task '{}'",
                                           handle, entry.writer->name()));
    }

    entry.writer = &task;
    return handle;
  }


  RenderSchedule RenderGraph::compile(RenderTask &goalTask) {
    for (const auto &entry : tasks) {
      entry.task->dependencies.clear();  // Clear the dependencies for each task
    }

    // Build dependencies based on resource usage
    for (const auto &[handle, resource] : resourceTable) {
      if (resource.writer) {
        for (auto *reader : resource.readers) {
          reader->dependencies.insert(resource.writer);
        }
      }
    }


    std::unordered_map<RenderTask *, int> levels;

    std::function<int(RenderTask *)> dfs = [&](RenderTask *task) -> int {
      if (levels.count(task)) return levels[task];

      int maxPredLevel = 0;
      for (auto pred : task->dependencies) {
        maxPredLevel = std::max(maxPredLevel, dfs(pred));
      }

      int level = maxPredLevel + 1;
      levels[task] = level;
      return level;
    };

    dfs(&goalTask);

    RenderSchedule schedule(*this);

    std::map<int, std::vector<RenderTask *>> levelTasks;
    int maxLevel = 0;
    int minLevel = std::numeric_limits<int>::max();
    for (const auto &[task, level] : levels) {
      levelTasks[level].push_back(task);
      maxLevel = std::max(maxLevel, level);
      minLevel = std::min(minLevel, level);
    }

    for (int level = minLevel; level <= maxLevel; ++level) {
      for (auto *task : levelTasks[level]) {
        schedule.addTask(task);
      }
    }
    return schedule;
  }

  void RenderGraph::runFor(RenderTask &goalTask) {
    fmt::println("Running render graph for goal task '{}'", goalTask.name());

    auto schedule = compile(goalTask);


    for (const auto &task : schedule.getTasks()) {
      fmt::println("  {}", task->toString());
    }



    // print dependencies for debug
    fmt::println("digraph {{");

    auto repr = [](RenderTask *task) { return fmt::format("{}.{}", task->name(), (void *)task); };

    // for (const auto &[task, level] : levels) {
    //   fmt::println("  \"{}\"[label=\"{} {}\"]", repr(task), task->name(), level);
    // }

    for (const auto &entry : tasks) {
      for (auto *dep : entry.task->dependencies) {
        fmt::println("  \"{}\" -> \"{}\";", repr(dep), repr(entry.task.get()));
      }
    }
    fmt::println("}}");
  }


}  // namespace ren
