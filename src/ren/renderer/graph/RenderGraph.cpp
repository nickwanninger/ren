#include <ren/renderer/graph/RenderGraph.h>

#include <imgui/imgui.h>
#include <ImGuizmo/ImGuizmo.h>
#include <ImGuizmo/GraphEditor.h>
#include <unordered_map>
#include <queue>
#include <chrono>

namespace ren {

  // Implementation of RenderGraph would go here


  //

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


  bool RenderGraph::startFrame(ref<ren::Image> swapchainImage) {
    auto newImageSize = glm::uvec2(swapchainImage->getWidth(), swapchainImage->getHeight());

    bool swapchainChanged = newImageSize != this->swapchainSize;
    this->swapchainSize = newImageSize; // update the stored size.

    bool anyReallocated = false;

    // we iterate over every resource and check if any of them need to be updated, despite having
    // this top level "swapchainChanged" flag. This is to handle iamges being added after the graph
    // is created, so all resources are present at the start of the frame.
    for (auto &[handle, resource] : resourceTable) {
      if (resource.type == GraphResourceType::Image) {
        auto &spec = resource.imageSpec;
        // TODO: temporal resources?

        bool relativeScale = !(spec.scale.x == 0.0f && spec.scale.y == 0.0f);
        // If the image is null (or we need to update because swapchain size changed and it's
        // relative)
        if ((resource.image == nullptr || (swapchainChanged && relativeScale))) {
          u32 width = spec.width;
          u32 height = spec.height;

          if (relativeScale) {
            width = static_cast<u32>(newImageSize.x * spec.scale.x);
            height = static_cast<u32>(newImageSize.y * spec.scale.y);
          }


          if (width < 1) width = 1;
          if (height < 1) height = 1;

          fmt::println("Allocating/reallocating image resource '{}' with size {}x{}", resource.name,
                       width, height);


          if (resource.image == nullptr) {
            fmt::println("  (was null, allocating new)");
          } else {
            fmt::println("  (swapchain size changed, reallocating)");
          }

          // Here we would allocate the actual image resource.
          // For now, we just log it.
          // In a real implementation, you'd create a ren::Image with the given size and format.


          ren::ImageBuilder b(resource.name);



          switch (resource.initialAccess) {
            case GraphAccess::RenderTarget:
              b.setUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                         VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
              break;
            case GraphAccess::DepthTarget:
              b.setUsage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                         VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
              break;
            default:
              b.setUsage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT);
              break;
          }

          b.setWidth(width)
              .setHeight(height)
              .setFormat(resource.imageSpec.format)
              .setTiling(VK_IMAGE_TILING_OPTIMAL)
              .setSamples(VK_SAMPLE_COUNT_1_BIT)
              .setMipLevels(1)
              .setArrayLayers(1)
              .setInitialLayout(VK_IMAGE_LAYOUT_UNDEFINED);

          resource.image = b.build();


          anyReallocated = true;
        }
      }
    }


    return anyReallocated;
  }

  GraphHandle RenderGraph::createImage(const std::string_view &name, const GraphImageSpec &spec,
                                       GraphAccess initialAccess) {
    GraphHandle handle = nextHandle++;

    ResourceEntry entry;
    entry.name = name;
    entry.type = GraphResourceType::Image;
    entry.initialAccess = initialAccess;
    entry.imageSpec = spec;

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
    auto &entry = resourceTable[handle];
    entry.users.insert(&task);
    return handle;
  }

  GraphHandle RenderGraph::addWrite(RenderTask &task, GraphHandle handle, GraphAccess access) {
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


  void RenderGraph::inspect() {
    ImGui::SetNextWindowSize(ImVec2(1200, 700), ImGuiCond_FirstUseEver);
    ImGui::Begin("Render Graph Inspector");

    // Static state for selected task and resource
    static RenderTask *selectedTask = nullptr;
    static GraphHandle selectedResource = nullGraphHandle;

    // Draw tab bar
    if (ImGui::BeginTabBar("InspectorTabs", ImGuiTabBarFlags_None)) {
      // ==================== TASKS TAB ====================
      if (ImGui::BeginTabItem("Tasks")) {
        ImGui::BeginGroup();
        {
          // Left panel: Task list
          ImGui::BeginChild("TaskListPanel", ImVec2(280, -ImGui::GetFrameHeightWithSpacing()),
                            ImGuiChildFlags_Border);
          {
            ImGui::Text("Tasks (%zu)", tasks.size());
            ImGui::Separator();

            for (const auto &entry : tasks) {
              RenderTask *task = entry.task.get();
              bool isSelected = (task == selectedTask);
              ImGui::PushID(task);
              if (ImGui::Selectable(entry.name.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
                selectedTask = task;
              }
              ImGui::PopID();
            }
          }
          ImGui::EndChild();
        }
        ImGui::EndGroup();

        ImGui::SameLine();

        // Right panel: Task details
        ImGui::BeginGroup();
        {
          ImGui::BeginChild("TaskDetailsPanel", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()),
                            ImGuiChildFlags_Border);
          {
            if (selectedTask) {
              ImGui::Text("Task: %s", selectedTask->name().c_str());
              ImGui::Separator();

              // Operands (reads)
              const auto &operands = selectedTask->getOperands();
              if (!operands.empty()) {
                if (ImGui::TreeNode("Operands (Reads)")) {
                  for (const auto &op : operands) {
                    const char *accessStr = fmt::formatter<GraphAccess>::toString(op.access);
                    const char *typeStr = fmt::formatter<GraphResourceType>::toString(op.resourceType);
                    ImGui::Text("%%%-3u : %s (type: %s)", op.valueHandle, accessStr, typeStr);

                    // Show resource name if available
                    auto it = resourceTable.find(op.valueHandle);
                    if (it != resourceTable.end() && !it->second.name.empty()) {
                      ImGui::SameLine();
                      ImGui::TextDisabled("(%s)", it->second.name.c_str());
                    }
                  }
                  ImGui::TreePop();
                }
              }

              // Results (writes)
              const auto &results = selectedTask->getResults();
              if (!results.empty()) {
                if (ImGui::TreeNode("Results (Writes)")) {
                  for (GraphHandle resultHandle : results) {
                    auto it = resourceTable.find(resultHandle);
                    if (it != resourceTable.end()) {
                      ImGui::Text("%%%-3u : %s", resultHandle, it->second.name.c_str());
                      ImGui::SameLine();
                      const char *accessStr = fmt::formatter<GraphAccess>::toString(it->second.writeAccess);
                      ImGui::TextDisabled("(%s)", accessStr);
                    } else {
                      ImGui::Text("%%%-3u : <unknown>", resultHandle);
                    }
                  }
                  ImGui::TreePop();
                }
              }

              // Dependencies
              if (!selectedTask->dependencies.empty()) {
                if (ImGui::TreeNode("Dependencies")) {
                  for (auto depTask : selectedTask->dependencies) {
                    ImGui::BulletText("%s", depTask->name().c_str());
                  }
                  ImGui::TreePop();
                }
              }

              // Custom toString for task
              ImGui::Separator();
              ImGui::TextWrapped("SSA Form: %s", selectedTask->toString().c_str());

            } else {
              ImGui::TextDisabled("(Select a task to view details)");
            }
          }
          ImGui::EndChild();
        }
        ImGui::EndGroup();

        ImGui::EndTabItem();
      }

      // ==================== RESOURCES TAB ====================
      if (ImGui::BeginTabItem("Resources")) {
        ImGui::BeginGroup();
        {
          // Left panel: Resource list
          ImGui::BeginChild("ResourceListPanel", ImVec2(280, -ImGui::GetFrameHeightWithSpacing()),
                            ImGuiChildFlags_Border);
          {
            ImGui::Text("Resources (%zu)", resourceTable.size());
            ImGui::Separator();

            for (const auto &[handle, entry] : resourceTable) {
              bool isSelected = (handle == selectedResource);
              ImGui::PushID(handle);
              if (ImGui::Selectable(entry.name.c_str(), isSelected)) {
                selectedResource = handle;
              }
              ImGui::PopID();
            }
          }
          ImGui::EndChild();
        }
        ImGui::EndGroup();

        ImGui::SameLine();

        // Right panel: Resource details
        ImGui::BeginGroup();
        {
          ImGui::BeginChild("ResourceDetailsPanel", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()),
                            ImGuiChildFlags_Border);
          {
            if (selectedResource != nullGraphHandle) {
              auto it = resourceTable.find(selectedResource);
              if (it != resourceTable.end()) {
                const auto &resEntry = it->second;
                ImGui::Text("Resource Handle: %%%-3u", selectedResource);
                ImGui::Text("Name: %s", resEntry.name.c_str());
                const char *typeStr = fmt::formatter<GraphResourceType>::toString(resEntry.type);
                const char *initialAccessStr = fmt::formatter<GraphAccess>::toString(resEntry.initialAccess);
                const char *writeAccessStr = fmt::formatter<GraphAccess>::toString(resEntry.writeAccess);
                ImGui::Text("Type: %s", typeStr);
                ImGui::Text("Initial Access: %s", initialAccessStr);
                ImGui::Text("Write Access: %s", writeAccessStr);

                if (resEntry.type == GraphResourceType::Image && resEntry.image) {
                  ImGui::Separator();
                  ImGui::Text("Image Details:");
                  ImGui::Text("  Format: %u", resEntry.imageSpec.format);
                  ImGui::Text("  Scale: (%.2f, %.2f)", resEntry.imageSpec.scale.x,
                              resEntry.imageSpec.scale.y);
                  if (resEntry.imageSpec.scale.x != 0 || resEntry.imageSpec.scale.y != 0) {
                    ImGui::Text("  Relative to swapchain");
                  } else {
                    ImGui::Text("  Absolute size: %u x %u", resEntry.imageSpec.width,
                                resEntry.imageSpec.height);
                  }
                  ImGui::Text("  Actual size: %u x %u", resEntry.image->getWidth(),
                              resEntry.image->getHeight());
                }

                ImGui::Separator();
                if (resEntry.definingTask) {
                  ImGui::Text("Defined by: %s", resEntry.definingTask->name().c_str());
                }

                if (!resEntry.users.empty()) {
                  if (ImGui::TreeNode("Used by:")) {
                    for (auto userTask : resEntry.users) {
                      ImGui::BulletText("%s", userTask->name().c_str());
                    }
                    ImGui::TreePop();
                  }
                }
              }
            } else {
              ImGui::TextDisabled("(Select a resource to view details)");
            }
          }
          ImGui::EndChild();
        }
        ImGui::EndGroup();

        ImGui::EndTabItem();
      }

      ImGui::EndTabBar();
    }
    ImGui::End();
  }


}  // namespace ren
