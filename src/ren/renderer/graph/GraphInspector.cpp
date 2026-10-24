#include <ren/renderer/graph/RenderGraph.h>
#include <ren/renderer/Renderer.h>
#include <imgui/imgui.h>
#include <unordered_set>

namespace ren {
  namespace {
    const char *accessName(GraphAccess access) {
      switch (access) {
        case GraphAccess::RenderTarget:
          return "RenderTarget";
        case GraphAccess::DepthTarget:
          return "DepthTarget";
        case GraphAccess::VertexShaderRead:
          return "VertexShaderRead";
        case GraphAccess::FragmentShaderRead:
          return "FragmentShaderRead";
        case GraphAccess::ComputeShaderRead:
          return "ComputeShaderRead";
        case GraphAccess::ComputeShaderWrite:
          return "ComputeShaderWrite";
        case GraphAccess::ComputeShaderReadWrite:
          return "ComputeShaderReadWrite";
        case GraphAccess::ShaderRead:
          return "ShaderRead";
      }
      return "Unknown";
    }
  }  // namespace


  void RenderGraph::inspect() {
    // The position of each task in the last frame.
    TaskPositions positions;
    for (size_t index = 0; index < lastOrder.size(); ++index) {
      if (auto task = lastOrder[index].lock()) {
        positions[task.get()] = index;
      }
    }

    // The task list on the left, and the selected task on the right.
    inspectTaskList(positions);
    ImGui::SameLine();
    inspectSelection();
  }


  void RenderGraph::inspectTaskList(const TaskPositions &positions) {
    // Every registered task, in creation order. The number is the position of
    // the task in the last frame; "-" means that it did not run.
    ImGui::BeginChild("GraphTaskList", ImVec2(220, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    auto selected = inspectedTask.lock();
    for (const auto &entry : registry) {
      auto task = entry.lock();
      if (!task) {
        continue;
      }

      auto position = positions.find(task.get());
      auto label = position != positions.end() ? fmt::format("{:>2}  {}", position->second, task->name()) : fmt::format(" -  {}", task->name());

      ImGui::PushID(task.get());
      if (ImGui::Selectable(label.c_str(), task == selected)) {
        inspectedTask = task;
        selected = task;
      }
      ImGui::PopID();
    }
    ImGui::EndChild();
  }


  void RenderGraph::inspectSelection() {
    ImGui::BeginChild("GraphTaskDetails", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (auto selected = inspectedTask.lock()) {
      inspectTask(*selected);
    } else {
      ImGui::TextDisabled("Select a task to see its details.");
    }
    ImGui::EndChild();
  }


  void RenderGraph::inspectTask(RenderTask &task) {
    ImGui::TextUnformatted(task.name().c_str());
    ImGui::Separator();

    // When the task ran, its view, and the cost of recording it.
    bool ran = std::any_of(lastOrder.begin(), lastOrder.end(), [&](const auto &entry) { return entry.lock().get() == &task; });
    if (ran) {
      ImGui::Text("Ran in the last frame");
    } else {
      ImGui::TextDisabled("Did not run in the last frame");
    }
    if (task.view) {
      ImGui::Text("View size: %u x %u", task.view->renderSize.x, task.view->renderSize.y);
    } else {
      ImGui::TextDisabled("No view");
    }
    if (task.numExecutions > 0) {
      ImGui::Text("CPU recording time: %.3f ms average over %llu frames", task.totalTimeNs / task.numExecutions / 1.0e6,
                  static_cast<unsigned long long>(task.numExecutions));
    }


    // The inputs and outputs, as the task declared them.
    if (ImGui::TreeNodeEx("Inputs", ImGuiTreeNodeFlags_DefaultOpen)) {
      for (const auto &input : task.operands) {
        inspectUsage(task, input, true);
      }
      if (task.operands.empty()) {
        ImGui::TextDisabled("None");
      }
      ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("Outputs", ImGuiTreeNodeFlags_DefaultOpen)) {
      for (const auto &output : task.results) {
        inspectUsage(task, output, false);
      }
      if (task.results.empty()) {
        ImGui::TextDisabled("None");
      }
      ImGui::TreePop();
    }


    ImGui::SeparatorText("Settings");
    task.inspect();


    // The images that the task writes. An update writes the storage of the
    // version that it updates, so each resource shows once.
    ImGui::SeparatorText("Images");
    std::unordered_set<const GraphResource *> shown;
    for (const auto &output : task.results) {
      if (shown.insert(validate(output.handle).resource.get()).second) {
        inspectImage(output.handle);
      }
    }
  }


  void RenderGraph::inspectUsage(const RenderTask &task, const GraphHandleUsage &usage, bool input) const {
    const auto &version = validate(usage.handle);

    // An update lists the version that it changes as an input.
    bool updated = input && std::any_of(task.results.begin(), task.results.end(),
                                        [&](const auto &output) { return validate(output.handle).prior == usage.handle.version; });

    std::string source;
    if (version.previous) {
      source = "history";
    } else if (updated) {
      source = "changed in place, from " + producerName(version.producer);
    } else if (input) {
      source = "from " + producerName(version.producer);
    }

    auto access = updated ? "Update" : accessName(usage.access);
    ImGui::BulletText("%s v%llu  %s", version.resource->name.c_str(), static_cast<unsigned long long>(version.id), access);
    if (!source.empty()) {
      ImGui::SameLine();
      ImGui::TextDisabled("(%s)", source.c_str());
    }
  }


  void RenderGraph::inspectImage(GraphHandle handle) const {
    auto resource = std::dynamic_pointer_cast<ImageResource>(validate(handle).resource);
    if (!resource) {
      return;  // Only images can be shown.
    }

    ImGui::TextUnformatted(resource->name.c_str());

    // The storage from the last frame where the producer ran. If this frame
    // allocates new storage, e.g. after a resize, the image shows it.
    auto storage = std::dynamic_pointer_cast<ImageStorage>(resource->current);
    if (!storage) {
      ImGui::TextDisabled("Not allocated yet");
      return;
    }

    const auto &image = *storage->image;
    ImGui::TextDisabled("%u x %u%s", image.getWidth(), image.getHeight(), resource->temporal ? ", temporal" : "");

    // Fit the image to the width of the panel.
    float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    float height = width * image.getHeight() / std::max(1u, image.getWidth());
    ImGui::Image(Renderer::get().getGraphImages().request(handle, true), ImVec2(width, height));
  }


  std::string RenderGraph::producerName(u64 task) const {
    if (task == 0) {
      return "no producer";
    }

    auto entry = producers.find(task);
    auto producer = entry == producers.end() ? nullptr : entry->second.lock();
    return producer ? producer->name() : "a removed task";
  }
}  // namespace ren
