#include <ren/renderer/graph/RenderGraph.h>
#include <ren/renderer/graph/ImageResource.h>
#include <ren/renderer/Swapchain.h>

#include <imgui/imgui.h>
#include <ImGuizmo/ImGuizmo.h>
#include <ImGuizmo/GraphEditor.h>

#include <unordered_map>
#include <unordered_set>
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

    this->swapchainSize = newImageSize;  // update the stored size.

    bool anyReallocated = false;
    std::unordered_set<RenderTask *> needToPrepare;

    // Update all resources with the new swapchain size.
    // Each resource will decide whether it needs to rebuild based on its spec.
    for (auto &[handle, resource] : resourceTable) {
      if (resource->update(*this)) {
        anyReallocated = true;
        fmt::println("Resource '{}' updated (handle {})", resource->name, handle);
        for (auto *userTask : resource->users) {
          needToPrepare.insert(userTask);
        }
        // Re-prepare the defining task as well, in case it needs to recreate things like
        // framebuffers.
        if (resource->definingTask != nullptr) needToPrepare.insert(resource->definingTask);
      }
    }

    if (needToPrepare.size() > 0) {
      getVulkan().waitForIdle(); // TEST
      for (auto *task : needToPrepare) {
        fmt::println("Re-preparing task '{}'", task->name());
        task->version++;
        // Re-prepare tasks whose resources were reallocated
        task->unprepare();
        task->prepare();
      }
    }

    return anyReallocated;
  }

  GraphHandle RenderGraph::createImage(const std::string_view &name, const GraphImageSpec &spec,
                                       GraphAccess initialAccess) {
    GraphHandle handle = nextHandle++;

    // if the spec has 0 scale, and 0 width/height, it's invalid.
    if (spec.scale == glm::vec2(0.0f) && spec.width == 0 && spec.height == 0) {
      throw std::runtime_error("Invalid GraphImageSpec: must have non-zero scale or fixed size");
    }

    auto imageResource = makeRef<ImageResource>(spec);
    imageResource->name = std::string(name);
    imageResource->initialAccess = initialAccess;

    // Image allocation is deferred until startFrame() calls update()
    resourceTable[handle] = imageResource;

    return handle;
  }

  GraphHandle RenderGraph::addRead(RenderTask &task, GraphHandle handle, GraphAccess access) {
    auto resource = resourceTable[handle];
    resource->users.insert(&task);
    return handle;
  }

  GraphHandle RenderGraph::addWrite(RenderTask &task, GraphHandle handle, GraphAccess access) {
    auto resource = resourceTable[handle];
    if (resource->definingTask != nullptr) {
      throw std::runtime_error(fmt::format("Warning: Resource {} already has a defining task '{}'",
                                           handle, resource->definingTask->name()));
    }

    resource->definingTask = &task;
    resource->writeAccess = access;  // Track what access state this resource is written to
    return handle;
  }


  // Compute data dependencies based on operand/result relationships
  // For each task, its dependencies are the tasks that write to resources it reads.
  void RenderGraph::computeDependencies(void) {
    // Clear existing dependencies
    for (const auto &task : tasks) {
      task->dependencies.clear();
    }

    // Build dependencies from resource flow:
    // For each resource, all readers depend on the writer
    for (const auto &[handle, resource] : resourceTable) {
      if (resource->definingTask != nullptr) {
        for (auto *reader : resource->users) {
          reader->dependencies.insert(resource->definingTask);
        }
      }
    }
  }

  RenderTask *RenderGraph::getDefiningTask(GraphHandle resourceHandle) {
    auto it = resourceTable.find(resourceHandle);
    if (it == resourceTable.end()) {
      throw std::runtime_error(fmt::format("Invalid resource handle: {}", resourceHandle));
    }

    auto *definingTask = it->second->definingTask;
    if (definingTask == nullptr) {
      throw std::runtime_error(
          fmt::format("Resource {} ('{}') has no defining task - it was never written to",
                      resourceHandle, it->second->name));
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
    for (const auto &[handle, resource] : resourceTable) {
      resourceStates[handle] = resource->initialAccess;
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
        if (it != resourceTable.end()) { resourceStates[resultHandle] = it->second->writeAccess; }
      }
    }

    // afterwards, add barriers to convert all resources into FragmentShaderRead for presentation
    // TODO: this should only happen if we want to read them in the ImGui debug.
    for (const auto &[handle, resource] : resourceTable) {
      GraphAccess currentState = resourceStates[handle];
      GraphAccess neededState = GraphAccess::ShaderRead;
      if (currentState != neededState) {
        auto *barrier = schedule.createBarrier(handle, currentState, neededState);
        schedule.addTask(barrier);
        resourceStates[handle] = neededState;
      }
    }

    // Copy task levels into the schedule
    schedule.taskLevels = taskLevels;

    return schedule;
  }

  void RenderGraph::runFor(GraphHandle goalResource, class Renderer &renderer) {
    auto it = resourceTable.find(goalResource);
    if (it == resourceTable.end()) {
      throw std::runtime_error(fmt::format("Invalid resource handle for runFor: {}", goalResource));
    }

    // fmt::println("Starting frame.");
    auto scheduleStart = std::chrono::high_resolution_clock::now();
    auto schedule = compile(goalResource);
    // schedule.validate();
    auto scheduleEnd = std::chrono::high_resolution_clock::now();

    // auto durationNs = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    // fmt::println("Compiled render graph schedule in {} ns", durationNs);

#if 0
    int currentLevel = 0;
    for (const auto &task : schedule.getTasks()) {
      if (schedule.getLevel(task) != currentLevel) {
        currentLevel = schedule.getLevel(task);
        fmt::println("{:04d}:", currentLevel);
      }
      fmt::println("  {}", task->toString());
    }
#endif

    auto runStart = std::chrono::high_resolution_clock::now();
    // Execute the schedule with the provided renderer
    GraphRunContext ctx(*this, renderer);
    ctx.cmd = ren::getFrameData().commandBuffer;
    for (const auto &task : schedule.getTasks()) {
      ctx.task = task;
      task->execute(ctx);
    }
    auto runEnd = std::chrono::high_resolution_clock::now();

    this->compileTimeUs +=
        std::chrono::duration_cast<std::chrono::microseconds>(scheduleEnd - scheduleStart).count();
    this->totalRuntimeUs +=
        std::chrono::duration_cast<std::chrono::microseconds>(runEnd - runStart).count();
    this->numRuns++;
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
            // ImGui::Text("Tasks (%zu)", tasks.size());
            // ImGui::Separator();

            for (const auto &task : tasks) {
              RenderTask *taskPtr = task.get();
              bool isSelected = (taskPtr == selectedTask);
              ImGui::PushID(taskPtr);
              if (ImGui::Selectable(task->name().c_str(), isSelected,
                                    ImGuiSelectableFlags_AllowDoubleClick)) {
                selectedTask = taskPtr;
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
              ImGui::Text("Task: %s (Version %d)", selectedTask->name().c_str(),
                          selectedTask->version);
              ImGui::Separator();
              ImGui::Text("Average Execution Time: %.2f ns over %llu runs",
                          selectedTask->averageTimeNs(), selectedTask->numExecutions);

              // Operands (reads)
              const auto &operands = selectedTask->getOperands();
              if (!operands.empty()) {
                if (ImGui::TreeNode("Operands (Reads)")) {
                  for (const auto &op : operands) {
                    const char *accessStr = fmt::formatter<GraphAccess>::toString(op.access);
                    const char *typeStr =
                        fmt::formatter<GraphResourceType>::toString(op.resourceType);
                    ImGui::Text("%%%-3u : %s (type: %s)", op.valueHandle, accessStr, typeStr);

                    // Show resource name if available
                    auto it = resourceTable.find(op.valueHandle);
                    if (it != resourceTable.end() && !it->second->name.empty()) {
                      ImGui::SameLine();
                      ImGui::TextDisabled("(%s)", it->second->name.c_str());
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
                      ImGui::Text("%%%-3u : %s", resultHandle, it->second->name.c_str());
                      ImGui::SameLine();
                      const char *accessStr =
                          fmt::formatter<GraphAccess>::toString(it->second->writeAccess);
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

              ImGui::Separator();

              selectedTask->inspect();

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

            for (const auto &[handle, resource] : resourceTable) {
              bool isSelected = (handle == selectedResource);
              ImGui::PushID(handle);
              if (ImGui::Selectable(resource->name.c_str(), isSelected)) {
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
                const auto &resource = it->second;
                ImGui::Text("Resource Handle: %%%-3u", selectedResource);
                ImGui::Text("Name: %s", resource->name.c_str());
                const char *typeStr = fmt::formatter<GraphResourceType>::toString(resource->type);
                const char *initialAccessStr =
                    fmt::formatter<GraphAccess>::toString(resource->initialAccess);
                const char *writeAccessStr =
                    fmt::formatter<GraphAccess>::toString(resource->writeAccess);
                ImGui::Text("Type: %s", typeStr);
                ImGui::Text("Initial Access: %s", initialAccessStr);
                ImGui::Text("Write Access: %s", writeAccessStr);

                // Delegate to resource-specific inspection
                ImGui::Separator();
                resource->inspect();

                ImGui::Separator();
                if (resource->definingTask) {
                  ImGui::Text("Defined by: %s", resource->definingTask->name().c_str());
                }

                if (!resource->users.empty()) {
                  if (ImGui::TreeNode("Used by:")) {
                    for (auto userTask : resource->users) {
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


      // Statistics tab.
      if (ImGui::BeginTabItem("Statistics")) {
        ImGui::Text("Runs: %llu", numRuns);
        double avgCompileMs = numRuns > 0 ? (double)compileTimeUs / numRuns / 1000.0 : 0.0;
        double avgRuntimeMs = numRuns > 0 ? (double)totalRuntimeUs / numRuns / 1000.0 : 0.0;
        ImGui::Text("Average Compile Time: %.3f ms", avgCompileMs);
        ImGui::Text("Average Runtime: %.3f ms", avgRuntimeMs);
        ImGui::EndTabItem();
      }

      ImGui::EndTabBar();
    }
    ImGui::End();
  }


}  // namespace ren
