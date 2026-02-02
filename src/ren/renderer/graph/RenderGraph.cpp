#include <ren/renderer/graph/RenderGraph.h>
#include <ren/renderer/graph/ImageResource.h>
#include <ren/renderer/Swapchain.h>

#include <imgui/imgui.h>
#include <ImGuizmo/ImGuizmo.h>
#include <ImGuizmo/GraphEditor.h>
#include <imnodes/imnodes.h>

#include <unordered_map>
#include <unordered_set>
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
    m_results.push_back(GraphHandleUsage{handle, access});
    return *this;
  }


  std::string RenderTask::toString(void) const {
    // print like an llvm SSA instruction
    std::stringstream ss;

    // Results (writes)
    int ind = 0;
    for (const auto &result : getResults()) {
      if (ind++ > 0) {
        ss << ", ";
      }
      ss << fmt::format("%{}", result.handle);
    }

    ss << " ← " << name() << "(";

    // Operands (reads)
    ind = 0;
    for (const auto &operand : getOperands()) {
      if (ind++ > 0) {
        ss << ", ";
      }
      ss << operand.toString();
    }

    ss << ")";

    return ss.str();
  }


  RenderGraph::RenderGraph() {}


  bool RenderGraph::startFrame(glm::uvec2 newImageSize) {
    // auto newImageSize = glm::uvec2(swapchainImage->getWidth(), swapchainImage->getHeight());

    this->swapchainSize = newImageSize;  // update the stored size.

    bool anyReallocated = false;
    std::unordered_set<RenderTask *> needToPrepare;

    // Update all resources with the new swapchain size.
    // Each resource will decide whether it needs to rebuild based on its spec.
    for (auto &[handle, resource] : resourceTable) {
      if (resource->update(*this)) {
        anyReallocated = true;
        for (auto *userTask : resource->users) {
          needToPrepare.insert(userTask);
        }
        // Re-prepare all writing tasks as well, in case they need to recreate things like framebuffers.
        for (auto *writer : resource->writingTasks) {
          needToPrepare.insert(writer);
        }
      }
    }

    if (needToPrepare.size() > 0) {
      getVulkan().waitForIdle();  // TEST
      for (auto *task : needToPrepare) {
        task->version++;
        // Re-prepare tasks whose resources were reallocated
        task->unprepare();
        task->prepare();
      }
    }

    return anyReallocated;
  }

  GraphHandle RenderGraph::createImage(const std::string_view &name, const GraphImageSpec &spec, GraphAccess initialAccess) {
    GraphHandle handle = nextHandle++;


    if (spec.relativeScale.isNone() && spec.absoluteSize.isNone()) {
      throw std::runtime_error("Invalid GraphImageSpec: must have non-zero scale or fixed size");
    }

    auto imageResource = make<ImageResource>(spec);
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
    resource->writingTasks.push_back(&task);
    return handle;
  }


  // Compute data dependencies based on operand/result relationships
  void RenderGraph::computeDependencies(void) {
    for (const auto &task : tasks) {
      task->dependencies.clear();
    }

    for (const auto &[handle, resource] : resourceTable) {
      auto &writers = resource->writingTasks;

      // Chain writers: writer[i] depends on writer[i-1]
      for (size_t i = 1; i < writers.size(); ++i) {
        writers[i]->dependencies.insert(writers[i - 1]);
      }

      // Readers depend on last writer
      if (!writers.empty()) {
        RenderTask *lastWriter = writers.back();
        for (auto *reader : resource->users) {
          // Avoid self-dependency if task reads and writes
          if (std::find(writers.begin(), writers.end(), reader) == writers.end()) {
            reader->dependencies.insert(lastWriter);
          }
        }
      }
    }
  }

  void RenderGraph::reportCycle(const std::vector<RenderTask *> &path) {
    std::stringstream ss;
    ss << "Cycle detected in render graph:\n";
    for (size_t i = 0; i < path.size(); ++i) {
      ss << "  " << path[i]->name();
      if (i + 1 < path.size()) {
        ss << " → ";
      }
    }
    throw std::runtime_error(ss.str());
  }

  void RenderGraph::detectAndReportCycle() {
    enum Color { WHITE, GRAY, BLACK };
    std::unordered_map<RenderTask *, Color> colors;
    std::vector<RenderTask *> path;

    std::function<bool(RenderTask *)> hasCycle = [&](RenderTask *task) -> bool {
      colors[task] = GRAY;
      path.push_back(task);

      for (auto *dep : task->dependencies) {
        if (colors[dep] == GRAY) {
          // Back edge found - we have a cycle
          path.push_back(dep);
          reportCycle(path);
          return true;
        }
        if (colors[dep] == WHITE && hasCycle(dep)) {
          return true;
        }
      }

      path.pop_back();
      colors[task] = BLACK;
      return false;
    };

    for (auto &task : tasks) {
      colors[task.get()] = WHITE;
    }

    for (auto &task : tasks) {
      if (colors[task.get()] == WHITE) {
        if (hasCycle(task.get())) {
          return;
        }
      }
    }
  }

  // Topological sort: DFS to order tasks respecting dependencies
  void RenderGraph::topologicalSort(RenderTask *task, std::vector<RenderTask *> &outOrder, std::unordered_set<RenderTask *> &visited) {
    if (task == nullptr || visited.count(task)) {
      return;
    }
    visited.insert(task);

    // Visit all dependencies first (pre-order traversal)
    for (auto *dep : task->dependencies) {
      topologicalSort(dep, outOrder, visited);
    }

    // Add this task after its dependencies
    outOrder.push_back(task);
  }

  RenderSchedule RenderGraph::compile() {
    REN_PROFILE_SCOPE("CompileRenderGraph");

    // Step 1: Compute dependencies from operand/result relationships
    computeDependencies();

    // Step 2: Find all root tasks (tasks with no dependencies)
    std::vector<RenderTask *> rootTasks;
    for (auto &task : tasks) {
      if (task->dependencies.empty()) {
        rootTasks.push_back(task.get());
      }
    }

    if (rootTasks.empty()) {
      // No roots means we have a cycle
      detectAndReportCycle();
      throw std::runtime_error("Cycle detected in render graph");
    }

    // Step 3: Topologically sort all tasks using Kahn's algorithm
    std::vector<RenderTask *> orderedTasks;
    std::unordered_map<RenderTask *, int> inDegree;

    // Calculate in-degree for each task
    for (auto &task : tasks) {
      inDegree[task.get()] = task->dependencies.size();
    }

    // Queue starts with all root tasks
    std::vector<RenderTask *> queue = rootTasks;

    // Process tasks in topological order
    while (!queue.empty()) {
      RenderTask *current = queue.back();
      queue.pop_back();
      orderedTasks.push_back(current);

      // Find all tasks that depend on current task
      for (auto &task : tasks) {
        if (task->dependencies.count(current)) {
          inDegree[task.get()]--;
          if (inDegree[task.get()] == 0) {
            queue.push_back(task.get());
          }
        }
      }
    }

    // Sanity check: all tasks scheduled
    if (orderedTasks.size() != tasks.size()) {
      throw std::runtime_error(
          fmt::format("Internal error: topological sort scheduled {} tasks but graph has {} tasks", orderedTasks.size(), tasks.size()));
    }

    // Step 4: Compute scheduling levels via DFS to determine parallelism
    std::unordered_map<RenderTask *, int> taskLevels;
    std::function<int(RenderTask *)> computeLevels = [&](RenderTask *task) -> int {
      if (taskLevels.count(task)) {
        return taskLevels[task];
      }

      int maxDepLevel = 0;
      for (auto *dep : task->dependencies) {
        maxDepLevel = std::max(maxDepLevel, computeLevels(dep));
      }

      int level = maxDepLevel + 1;
      taskLevels[task] = level;
      return level;
    };

    for (auto &task : tasks) {
      computeLevels(task.get());
    }

    // Step 5: Sort tasks by level (ascending) so all tasks of the same level are grouped together.
    // This allows executing all tasks of a level before moving to the next level.
    std::sort(orderedTasks.begin(), orderedTasks.end(), [&taskLevels](RenderTask *a, RenderTask *b) {
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
      for (const auto &result : task->getResults()) {
        resourceStates[result.handle] = result.access;
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

  void RenderGraph::printDot() const {
    ren::println("digraph RenderGraph {{");
    ren::println("  rankdir=LR;");
    ren::println("  node [shape=box];");
    ren::println("");

    // Print tasks
    ren::println("  // Tasks");
    for (const auto &task : tasks) {
      ren::println("  \"{}\" [style=filled, fillcolor=lightblue];", task->name());
    }
    ren::println("");

    // Print resources
    ren::println("  // Resources");
    for (const auto &[handle, resource] : resourceTable) {
      ren::println("  \"res_{}\" [label=\"{}\", shape=ellipse];", handle, resource->name);
    }
    ren::println("");

    // Print write edges (task -> resource)
    ren::println("  // Write edges");
    for (const auto &[handle, resource] : resourceTable) {
      for (auto *writer : resource->writingTasks) {
        ren::println("  \"{}\" -> \"res_{}\" [color=blue, label=\"W\"];", writer->name(), handle);
      }
    }
    ren::println("");

    // Print read edges (resource -> task)
    ren::println("  // Read edges");
    for (const auto &[handle, resource] : resourceTable) {
      for (auto *reader : resource->users) {
        ren::println("  \"res_{}\" -> \"{}\" [color=green, label=\"R\"];", handle, reader->name());
      }
    }
    ren::println("");

    // Print task dependencies
    ren::println("  // Task dependencies");
    for (const auto &task : tasks) {
      for (auto *dep : task->dependencies) {
        ren::println("  \"{}\" -> \"{}\" [color=red, style=dashed];", dep->name(), task->name());
      }
    }

    ren::println("}}");
  }

  void RenderGraph::run(class Renderer &renderer) {
    REN_PROFILE_SCOPE("RunRenderGraph");

    auto scheduleStart = std::chrono::high_resolution_clock::now();
    auto schedule = compile();
    auto scheduleEnd = std::chrono::high_resolution_clock::now();

#if 0
    int currentLevel = 0;
    for (const auto &task : schedule.getTasks()) {
      if (schedule.getLevel(task) != currentLevel) {
        currentLevel = schedule.getLevel(task);
        ren::println("{:04d}:", currentLevel);
      }
      ren::println("  {}", task->toString());
    }
#endif

    auto runStart = std::chrono::high_resolution_clock::now();
    // Execute the schedule with the provided renderer
    auto &frame = ren::getFrameUnit();
    GraphRunContext ctx(*this, renderer, *frame.getMainCommandEncoder());
    ctx.cmd = frame.getMainCommandEncoder()->buf();
    for (const auto &task : schedule.getTasks()) {
      ctx.task = task;
      task->execute(ctx);
    }

    this->compileTimeUs += std::chrono::duration_cast<std::chrono::microseconds>(scheduleEnd - scheduleStart).count();
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
      if (ImGui::BeginTabItem("Schedule")) {
        ImGui::Text("Render Graph Schedule");
        ImGui::Separator();
        auto start = std::chrono::high_resolution_clock::now();
        auto sched = compile();
        auto end = std::chrono::high_resolution_clock::now();
        float compileTimeMs = std::chrono::duration<float, std::chrono::milliseconds::period>(end - start).count();
        ImGui::Text("Compile Time: %.3f ms", compileTimeMs);
        ImGui::Separator();



        int next_id = 0;

        std::map<RenderTask *, int> taskIDs;
        for (auto *task : sched.getTasks()) {
          int level = sched.getLevel(task);
          if (level == -1) continue;

          taskIDs[task] = next_id++;
        }



        // Resources:
        // ImGui::Text("Resources:");
        // for (const auto &[handle, resource] : resourceTable) {
        //   ImGui::Text("%%%-3u : %s", handle, resource->name.c_str());
        // }

        // Tasks:
        for (const auto &task : sched.getTasks()) {
          int level = sched.getLevel(task);
          ImGui::Text("[%d] %s", level, task->toString().c_str());
        }
        ImGui::EndTabItem();

        ImNodes::BeginNodeEditor();


        ImVec2 location(0, 0);
        auto &tasks = sched.getTasks();
        int last_level = -100;  // magic number.
        for (auto *task : sched.getTasks()) {
          int id = taskIDs[task];
          int level = sched.getLevel(task);
          if (level == -1) continue;

          ImNodes::BeginNode(id);
          ImNodes::BeginNodeTitleBar();
          ImGui::TextUnformatted(task->name().c_str());
          ImNodes::EndNodeTitleBar();

          ImGui::Text("Task: %d, Level: %d", id, level);

          ImNodes::BeginInputAttribute(next_id++, ImNodesPinShape_Triangle);
          ImGui::TextUnformatted("Input 1");
          ImNodes::EndInputAttribute();

          ImNodes::BeginInputAttribute(next_id++, ImNodesPinShape_TriangleFilled);
          ImGui::TextUnformatted("Input 2");
          ImNodes::EndInputAttribute();


          ImNodes::BeginOutputAttribute(next_id++, ImNodesPinShape_Circle);
          ImGui::TextUnformatted("Output 1");
          ImNodes::EndOutputAttribute();
          ImNodes::BeginOutputAttribute(next_id++, ImNodesPinShape_CircleFilled);
          ImGui::TextUnformatted("Output 2");
          ImNodes::EndOutputAttribute();


          ImNodes::EndNode();

          ImNodes::SnapNodeToGrid(id);

          // ImNodes::SetNodeGridSpacePos(id, location);

          if (level != last_level) {
            location.x += 140;
            location.y = 0;
            last_level = level;
          } else {
            location.y += 220;
          }
        }
        // ImNodes::MiniMap();
        ImNodes::EndNodeEditor();
      }


      // ==================== TASKS TAB ====================
      if (ImGui::BeginTabItem("Tasks")) {
        ImGui::BeginGroup();
        {
          // Left panel: Task list
          ImGui::BeginChild("TaskListPanel", ImVec2(150, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_Border);
          {
            // ImGui::Text("Tasks (%zu)", tasks.size());
            // ImGui::Separator();

            for (const auto &task : tasks) {
              RenderTask *taskPtr = task.get();
              bool isSelected = (taskPtr == selectedTask);
              ImGui::PushID(taskPtr);
              if (ImGui::Selectable(task->name().c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
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
          ImGui::BeginChild("TaskDetailsPanel", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_Border);
          {
            if (selectedTask) {
              ImGui::Text("Task: %s (Version %d)", selectedTask->name().c_str(), selectedTask->version);
              ImGui::Separator();
              ImGui::Text("Average Execution Time: %.8f ms over %llu runs", selectedTask->averageTimeNs() / 1024.0f / 1024.0f,
                          (u64)selectedTask->numExecutions);

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
                  for (const auto &result : results) {
                    auto it = resourceTable.find(result.handle);
                    if (it != resourceTable.end()) {
                      ImGui::Text("%%%-3u : %s", result.handle, it->second->name.c_str());
                      ImGui::SameLine();
                      const char *accessStr = fmt::formatter<GraphAccess>::toString(result.access);
                      ImGui::TextDisabled("(%s)", accessStr);
                    } else {
                      ImGui::Text("%%%-3u : <unknown>", result.handle);
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
#if 0
        ImGui::BeginGroup();
        {
          // Left panel: Resource list
          ImGui::BeginChild("ResourceListPanel", ImVec2(150, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_Border);
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
          ImGui::BeginChild("ResourceDetailsPanel", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_Border);
          {
            if (selectedResource != nullGraphHandle) {
              auto it = resourceTable.find(selectedResource);
              if (it != resourceTable.end()) {
                const auto &resource = it->second;
                ImGui::Text("Resource Handle: %%%-3u", selectedResource);
                ImGui::Text("Name: %s", resource->name.c_str());
                const char *typeStr = fmt::formatter<GraphResourceType>::toString(resource->type);
                const char *initialAccessStr = fmt::formatter<GraphAccess>::toString(resource->initialAccess);
                ImGui::Text("Type: %s", typeStr);
                ImGui::Text("Initial Access: %s", initialAccessStr);

                // Delegate to resource-specific inspection
                ImGui::Separator();
                resource->inspect();

                ImGui::Separator();
                if (!resource->writingTasks.empty()) {
                  if (ImGui::TreeNode("Written by:")) {
                    for (auto *writer : resource->writingTasks) {
                      ImGui::BulletText("%s", writer->name().c_str());
                    }
                    ImGui::TreePop();
                  }
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
#endif


        if (resourceTable.empty()) {
          ImGui::TextDisabled("(No resources)");
        } else {
          if (ImGui::BeginTable("ResourceTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Handle", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableHeadersRow();

            for (const auto &[handle, resource] : resourceTable) {
              ImGui::PushID((int)handle);
              ImGui::TableNextRow();

              // If hovering on the row
              ImGui::TableNextColumn();
              if (ImGui::Selectable(resource->name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                // Handle row click if needed
              }

              if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                resource->inspect();
                ImGui::EndTooltip();
              }

              ImGui::TableNextColumn();
              ImGui::Text("%%%-3u", handle);

              ImGui::TableNextColumn();
              switch (resource->type) {
                case GraphResourceType::Image:
                  ImGui::Text("Image");
                  break;
                case GraphResourceType::Buffer:
                  ImGui::Text("Buffer");
                  break;
                default:
                  ImGui::Text("???");
                  break;
              }

              ImGui::PopID();
            }

            ImGui::EndTable();
          }
        }



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
