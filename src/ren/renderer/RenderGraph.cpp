#include <ren/renderer/RenderGraph.h>

#include <imgui/imgui.h>
#include <ImGuizmo/ImGuizmo.h>
#include <ImGuizmo/GraphEditor.h>
#include <unordered_map>
#include <queue>

namespace ren {

  // Implementation of RenderGraph would go here


  RenderTask &RenderTask::read(GraphHandle handle, GraphAccess access) {
    m_graph.addRead(*this, handle, access);
    return *this;
  }

  RenderTask &RenderTask::write(GraphHandle handle, GraphAccess access) {
    m_graph.addWrite(*this, handle, access);
    return *this;
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
    entry.name = std::string(name);
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


  void RenderGraph::runFor(RenderTask &goalTask) {
    fmt::println("Running render graph for goal task '{}'", goalTask.name());

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


    std::vector<RenderTask *> schedule;


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
        schedule.push_back(task);
      }
    }


    // Execute the scheduled tasks
    for (auto *task : schedule) {
      fmt::println(" - task '{}' at level {}", task->name(), levels[task]);
      // GraphRunContext ctx(*this);
      // ctx.task = task;
      // task->run(ctx);
    }




    // print dependencies for debug
    fmt::println("digraph {{");

    auto repr = [](RenderTask *task) { return fmt::format("{}.{}", task->name(), (void *)task); };

    for (const auto &[task, level] : levels) {
      fmt::println("  \"{}\"[label=\"{} {}\"]", repr(task), task->name(), level);
    }

    for (const auto &entry : tasks) {
      for (auto *dep : entry.task->dependencies) {
        fmt::println("  \"{}\" -> \"{}\";", repr(dep), repr(entry.task.get()));
      }
    }
    fmt::println("}}");
  }


}  // namespace ren
