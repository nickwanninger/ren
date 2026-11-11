#include <ren/renderer/graph/RenderGraph.h>
#include <ren/renderer/Renderer.h>
#include <ren/core/Application.h>
#include <imgui/imgui.h>
#include <algorithm>
#include <unordered_set>

namespace ren {
  ////////////////////////////////////////////////////////////////////////////
  // Handles and resources
  ////////////////////////////////////////////////////////////////////////////


  void RenderGraph::requireUpdate() const {
    if (phase != Phase::Update) {
      throw std::runtime_error("Graph mutation requires the update phase");
    }
  }


  const GraphVersion &RenderGraph::validate(GraphHandle handle) const {
    if (!handle.version || handle.version->owner.lock() != identity) {
      throw std::runtime_error("Invalid or foreign graph handle");
    }
    return *handle.version;
  }


  ref<GraphResource> RenderGraph::getResource(GraphHandle handle) const { return validate(handle).resource; }


  GraphHandle RenderGraph::createResource(std::string_view name, ref<GraphResource> resource) {
    requireUpdate();
    if (!resource) {
      throw std::runtime_error("Cannot create a null graph resource");
    }
    if (!resource->registeredOwner.expired()) {
      throw std::runtime_error("Resource already belongs to a graph");
    }

    resource->registeredOwner = identity;
    resource->name = name;

    // The first version has no producer until a task writes it.
    auto version = make<GraphVersion>();
    version->owner = identity;
    version->resource = std::move(resource);
    version->id = nextVersion++;
    return GraphHandle(version);
  }


  GraphHandle RenderGraph::createImage(std::string_view name, const GraphImageSpec &spec, GraphAccess initial) {
    return createResource(name, make<ImageResource>(spec, initial));
  }


  GraphHandle RenderGraph::previous(GraphHandle handle) const {
    const auto &value = validate(handle);
    if (!value.resource->temporal) {
      throw std::runtime_error("History requires an explicitly temporal resource: " + value.resource->name);
    }
    if (value.previous || (value.prior && !value.successor.expired())) {
      throw std::runtime_error("Intermediate history snapshots are unsupported: " + value.resource->name);
    }

    // A history handle has the ID of its version, but resolves to the history storage.
    auto history = make<GraphVersion>();
    history->owner = identity;
    history->resource = value.resource;
    history->id = value.id;
    history->previous = true;
    return GraphHandle(history);
  }



  ////////////////////////////////////////////////////////////////////////////
  // Tasks and their declarations
  ////////////////////////////////////////////////////////////////////////////


  RenderTask::RenderTask(RenderGraph &graph)
      : owner(graph)
      , id(graph.nextTask++) {
    graph.requireUpdate();
  }


  void RenderGraph::requireDeclarable(const RenderTask &task) const {
    requireUpdate();
    if (task.sealed) {
      throw std::runtime_error("Task dependencies are fixed; replace the task");
    }
    if (&task.owner != this) {
      throw std::runtime_error("Foreign graph task");
    }
  }


  void RenderGraph::declareRead(RenderTask &task, GraphHandle handle, GraphAccess access) {
    requireDeclarable(task);
    validate(handle);
    if (graphWrites(access)) {
      throw std::runtime_error("A read requires read access");
    }

    task.operands.push_back({handle, access});
  }


  void RenderGraph::declareWrite(RenderTask &task, GraphHandle handle, GraphAccess access) {
    requireDeclarable(task);
    validate(handle);
    auto &value = *handle.version;
    if (!graphWrites(access) || value.previous) {
      throw std::runtime_error("Invalid graph write");
    }
    if (value.producer) {
      throw std::runtime_error("Resource already has a producer; use updates(): " + value.resource->name);
    }

    value.producer = task.id;
    task.results.push_back({handle, access});
  }


  GraphHandle RenderGraph::declareUpdate(RenderTask &task, GraphHandle handle, GraphAccess access) {
    requireDeclarable(task);
    const auto &old = validate(handle);
    if (old.previous || !graphWrites(access)) {
      throw std::runtime_error("Invalid graph update");
    }

    // A version can have only one update. A second update is a branch.
    if (auto successor = old.successor.lock()) {
      auto entry = producers.find(successor->producer);
      auto first = entry == producers.end() ? nullptr : entry->second.lock();
      throw std::runtime_error(
          fmt::format("Branched update of '{}': '{}' and '{}'", old.resource->name, first ? first->name() : "expired task", task.name()));
    }

    auto next = make<GraphVersion>();
    next->owner = identity;
    next->resource = old.resource;
    next->id = nextVersion++;
    next->producer = task.id;
    next->prior = handle.version;
    handle.version->successor = next;

    // The old version is an operand so that its producer is selected and runs first.
    GraphHandle result(next);
    task.operands.push_back({handle, GraphAccess::ShaderRead});
    task.results.push_back({result, access});
    return result;
  }




  ////////////////////////////////////////////////////////////////////////////
  // Selection
  ////////////////////////////////////////////////////////////////////////////


  void RenderGraph::enqueue(RenderTask &task) {
    requireUpdate();
    if (&task.owner != this) {
      throw std::runtime_error("Cannot enqueue a foreign task");
    }

    auto registered = producers.find(task.id);
    if (registered == producers.end() || registered->second.expired()) {
      throw std::runtime_error("Task is not registered");
    }
    roots.push_back(registered->second.lock());
  }


  RenderSchedule RenderGraph::compile() {
    REN_PROFILE_SCOPE("Graph Compile");
    // Select the roots and the producers of their inputs, transitively. A
    // history read does not select its producer.
    std::unordered_map<u64, ref<RenderTask>> selected;
    std::vector<ref<RenderTask>> pending = roots;
    for (size_t index = 0; index < pending.size(); ++index) {
      auto task = pending[index];
      if (!selected.emplace(task->id, task).second) {
        continue;
      }

      for (const auto &input : task->operands) {
        const auto &version = validate(input.handle);
        if (version.previous) {
          continue;
        }

        auto producer = producers.find(version.producer);
        auto source = producer == producers.end() ? nullptr : producer->second.lock();
        if (!source) {
          throw std::runtime_error(
              fmt::format("Task '{}' needs missing producer for '{}' version {}", task->name(), version.resource->name, version.id));
        }
        pending.push_back(source);
      }
    }


    // Add the dependencies. A task depends on the producer of each input. An
    // update also depends on the selected readers of the old version.
    std::unordered_map<u64, std::unordered_set<u64>> dependencies;
    for (const auto &[id, task] : selected) {
      for (const auto &input : task->operands) {
        const auto &version = validate(input.handle);
        if (!version.previous) {
          dependencies[id].insert(version.producer);
        }
      }

      for (const auto &output : task->results) {
        const auto &version = validate(output.handle);
        if (!version.prior) {
          continue;
        }

        // Selected readers of the old version finish before this update.
        for (const auto &[readerID, reader] : selected) {
          if (readerID == id) {
            continue;
          }
          for (const auto &input : reader->operands) {
            if (input.handle.version == version.prior) {
              dependencies[id].insert(readerID);
            }
          }
        }
      }
    }


    // Order the tasks. Each pass adds the tasks whose dependencies are done,
    // in registration order. A pass that adds no task means a cycle.
    RenderSchedule result;
    std::unordered_set<u64> done;
    while (done.size() != selected.size()) {
      bool progress = false;
      for (const auto &entry : registry) {
        auto task = entry.lock();
        if (!task || !selected.contains(task->id) || done.contains(task->id)) {
          continue;
        }

        const auto &required = dependencies[task->id];
        if (!std::all_of(required.begin(), required.end(), [&](u64 id) { return done.contains(id); })) {
          continue;
        }

        result.tasks.push_back(task);
        done.insert(task->id);
        progress = true;
      }

      if (!progress) {
        std::string message = "Graph dependency cycle:";
        for (const auto &[id, task] : selected) {
          if (done.contains(id)) {
            continue;
          }
          message += " " + task->name() + " [";
          for (const auto &input : task->operands) {
            message += validate(input.handle).resource->name + " ";
          }
          message += "]";
        }
        throw std::runtime_error(message);
      }
    }


    // The dependencies of the selected tasks are now fixed.
    for (auto &task : result.tasks) {
      task->sealed = true;
    }
    return result;
  }



  ////////////////////////////////////////////////////////////////////////////
  // Preparation
  ////////////////////////////////////////////////////////////////////////////


  glm::uvec2 ImageSize::resolve(const CapturedView *view) const {
    if (!scale) {
      return glm::max(pixels, glm::uvec2(1));
    }

    if (!view) {
      throw std::runtime_error("View-relative image requires a ViewContext");
    }
    if (!std::isfinite(scale->x) || !std::isfinite(scale->y) || scale->x <= 0 || scale->y <= 0) {
      throw std::runtime_error("Image scale must be finite and positive");
    }

    // Round down, then clamp each dimension to one pixel.
    return glm::max(glm::uvec2(glm::vec2(view->values.renderSize) * *scale), glm::uvec2(1));
  }


  namespace {
    // Copy the values of a view, and write its FrameGlobals into storage that
    // the frame owns. Later changes to the ViewContext do not change this frame.
    ref<CapturedView> capture(const ViewContext &view, Renderer *renderer, SubmissionUnit *submission) {
      auto result = make<CapturedView>();
      result->values = view;
      result->values.renderSize = glm::max(view.renderSize, glm::uvec2(1));

      glm::vec2 size(result->values.renderSize);
      result->globals = FrameGlobals{
          .time = renderer ? static_cast<float>(Application::get().timeSeconds) : 0.0f,
          .deltaTime = renderer ? ren::deltaTime() : 0.0f,
          .frameNumber = renderer ? static_cast<u32>(getVulkan().frame_number) : 0,
          .renderSize = size,
          .inverseRenderSize = 1.0f / size,
          .viewMatrix = view.view,
          .projMatrix = view.projection,
      };

      // Tests have no submission, so there is no GPU copy of the globals.
      if (submission && renderer) {
        auto *buffer = submission->getArena().push<UniformBuffer<FrameGlobals>>(1);
        buffer->hostData()[0] = result->globals;

        if (!submission->getDescriptorAllocator().allocate(&result->globalsSet, renderer->getFrameGlobalsAllocator().getLayout())) {
          throw std::runtime_error("Cannot allocate captured globals descriptor");
        }

        VkDescriptorBufferInfo info{.buffer = buffer->getHandle(), .offset = 0, .range = sizeof(FrameGlobals)};
        VkWriteDescriptorSet write{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                   .dstSet = result->globalsSet,
                                   .dstBinding = ShaderABI::frameBinding,
                                   .descriptorCount = 1,
                                   .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                   .pBufferInfo = &info};
        vkUpdateDescriptorSets(getVulkan().device, 1, &write, 0, nullptr);
      }
      return result;
    }
  }  // namespace


  ref<GraphStorage> RenderGraph::assignStorage(GraphResource &definition, const CapturedView *view) {
    // A resource that is not temporal keeps its storage while it stays compatible.
    if (!definition.temporal && definition.current && definition.compatible(*definition.current, view)) {
      return definition.current;
    }

    // Temporal resources alternate between the history and one other allocation.
    // Work on the single graphics queue runs in submission order, and the recorded
    // transitions order this write after earlier reads of the same allocation.
    // Submissions retain obsolete entries, for example after a resize, until they complete.
    std::erase_if(definition.pool, [&](const auto &storage) { return !definition.compatible(*storage, view); });
    for (auto &storage : definition.pool) {
      if (storage != definition.history) {
        return storage;
      }
    }

    auto storage = definition.allocate(view);
    if (!storage) {
      throw std::runtime_error("Resource allocation returned null: " + definition.name);
    }
    definition.pool.push_back(storage);
    return storage;
  }


  void RenderGraph::prepareFrame(Renderer *renderer, SubmissionUnit *unit) {
    REN_PROFILE_SCOPE("Graph Prepare");
    requireUpdate();
    submission = unit;
    phase = Phase::Select;

    try {
      schedule = compile();

      // Capture each view once, before any task prepares.
      for (auto &task : schedule.tasks) {
        if (task->view && !views.contains(task->view.get())) {
          views.emplace(task->view.get(), capture(*task->view, renderer, submission));
        }
      }


      phase = Phase::Prepare;

      // Inputs start with the storage of the last frame. The outputs of this
      // frame replace it below.
      for (auto &task : schedule.tasks) {
        for (const auto &input : task->operands) {
          auto definition = validate(input.handle).resource;
          auto &mapping = mappings[definition.get()];
          mapping.definition = definition;
          mapping.current = definition->current;
          mapping.previous = definition->history;
        }
      }


      // Assign storage to the outputs of each task, then prepare the task.
      // Producers come first in the schedule, so the storage of each input is
      // ready before its consumer prepares.
      for (auto &task : schedule.tasks) {
        const CapturedView *view = task->view ? views.at(task->view.get()).get() : nullptr;
        for (const auto &output : task->results) {
          const auto &version = validate(output.handle);
          if (version.prior) {
            continue;  // An update uses the storage of the version that it updates.
          }

          auto definition = version.resource;
          auto &mapping = mappings[definition.get()];
          mapping.definition = definition;

          // History of a different size is not valid.
          mapping.previous = definition->history;
          if (mapping.previous && !definition->compatible(*mapping.previous, view)) {
            mapping.previous.reset();
          }

          mapping.current = assignStorage(*definition, view);
          mapping.produced = true;
        }

        PrepareContext ctx{ResourceResolver(*this), view, renderer, submission};
        task->prepareFrame(ctx);
      }


      // The submission keeps everything that the frame uses until the GPU completes it.
      if (submission) {
        for (auto &task : schedule.tasks) {
          submission->retain(task);
        }
        for (auto &[key, view] : views) {
          submission->retain(view);
        }
        for (auto &[key, mapping] : mappings) {
          submission->retain(mapping.current);
          submission->retain(mapping.previous);
        }
      }

      phase = Phase::Prepared;
    } catch (...) {
      discard();
      throw;
    }
  }


  ref<GraphStorage> RenderGraph::resolve(GraphHandle handle) const {
    const auto &version = validate(handle);
    if (phase == Phase::Update || phase == Phase::Select) {
      throw std::runtime_error("Resource resolution requires graph preparation");
    }

    auto it = mappings.find(version.resource.get());
    if (it != mappings.end()) {
      return version.previous ? it->second.previous : it->second.current;
    }

    // A resource that is not in this frame keeps the storage of the last frame where it ran.
    return version.previous ? version.resource->history : version.resource->current;
  }


  ref<GraphStorage> ResourceResolver::storage(GraphHandle handle) const { return source.resolve(handle); }


  ImageRef ResourceResolver::getImage(GraphHandle handle) const {
    auto value = storage(handle);
    return value ? get<ImageStorage>(handle)->image : nullptr;
  }


  TextureHandle ResourceResolver::texture(GraphHandle handle) const { return get<ImageStorage>(handle)->textureHandle(); }


  ref<ImageStorage> RenderGraph::displayImage(GraphHandle handle) {
    if (phase != Phase::Prepared) {
      throw std::runtime_error("Display lookup requires the prepared phase");
    }

    auto resource = get<ImageResource>(handle);
    auto storage = resolve(handle);
    if (!storage) {
      if (validate(handle).previous) {
        return nullptr;  // The history is not available.
      }
      throw std::runtime_error("Current image is unresolved: " + resource->name);
    }

    auto image = std::dynamic_pointer_cast<ImageStorage>(storage);
    if (!image) {
      throw std::runtime_error("Display resource is not image storage");
    }

    // The image changes to a shader-read layout at the end of the graph work.
    external.push_back(image);
    if (submission) {
      submission->retain(image);
    }
    return image;
  }



  ////////////////////////////////////////////////////////////////////////////
  // Barriers and recording
  ////////////////////////////////////////////////////////////////////////////


  void RenderGraph::appendAccess(ref<GraphStorage> storage, GraphAccess access) {
    if (!storage) {
      return;  // First or invalidated history.
    }

    // The access state starts from the state that the last submission saved.
    auto [it, inserted] = pendingAccess.try_emplace(storage.get(), storage->access);
    auto &from = it->second;
    schedule.operations.push_back(ScheduledResourceTransition{storage, from, access});

    // Keep every reader stage until the next write.
    if (graphWrites(access) || std::any_of(from.begin(), from.end(), graphWrites)) {
      from.clear();
    }
    if (std::find(from.begin(), from.end(), access) == from.end()) {
      from.push_back(access);
    }
  }


  const RenderSchedule &RenderGraph::compileTransitions() {
    if (phase != Phase::Prepared) {
      throw std::runtime_error("Transition compilation requires graph preparation");
    }

    try {
      // Before each task, add a barrier for each input and output.
      for (auto &task : schedule.tasks) {
        for (const auto &input : task->operands) {
          // An update consumes the prior contents through its write access, not a shader read.
          bool updateInput = std::any_of(task->results.begin(), task->results.end(),
                                         [&](const auto &output) { return validate(output.handle).prior == input.handle.version; });
          if (!updateInput) {
            appendAccess(resolve(input.handle), input.access);
          }
        }

        for (const auto &output : task->results) {
          appendAccess(resolve(output.handle), output.access);
        }

        schedule.operations.push_back(ScheduledRenderTask{task});
      }

      // After all tasks, images for displayImage() change to a shader-read layout.
      for (auto &storage : external) {
        appendAccess(storage, GraphAccess::ShaderRead);
      }

      phase = Phase::Compiled;
      return schedule;
    } catch (...) {
      discard();
      throw;
    }
  }


  void RenderGraph::record() {
    REN_PROFILE_SCOPE("RenderGraph::record");

    if (phase == Phase::Prepared) {
      compileTransitions();
    }
    if (phase != Phase::Compiled) {
      throw std::runtime_error("Recording requires compiled transitions");
    }

    try {
      phase = Phase::Record;
      auto &encoder = *getFrameUnit().getMainCommandEncoder();

      for (auto &operation : schedule.operations) {
        if (auto *transition = std::get_if<ScheduledResourceTransition>(&operation)) {
          RecordContext ctx{ResourceResolver(*this), nullptr, encoder};
          transition->storage->transition(ctx, transition->from, transition->to);
        } else {
          REN_PROFILE_SCOPE("RTexecute");
          // Each task uses the captured FrameGlobals of its view.
          auto task = std::get<ScheduledRenderTask>(operation).task;
          auto view = task->view ? views.at(task->view.get()).get() : nullptr;
          encoder.setFrameGlobals(view ? view->globalsSet : VK_NULL_HANDLE);

          RecordContext ctx{ResourceResolver(*this), view, encoder};
          task->execute(ctx);
        }
      }

      // Later work on this encoder uses the default FrameGlobals of the frame.
      encoder.setFrameGlobals(VK_NULL_HANDLE);
      phase = Phase::Recorded;
    } catch (...) {
      discard();
      throw;
    }
  }



  ////////////////////////////////////////////////////////////////////////////
  // End of frame
  ////////////////////////////////////////////////////////////////////////////


  void RenderGraph::submitted() {
    // Nothing was recorded, e.g. the frame was discarded. Drop the work of
    // this frame, and do not save state that no command produced.
    if (phase != Phase::Recorded && phase != Phase::Compiled) {
      if (!roots.empty()) {
        ren::warnln("Enqueued graph work was not recorded in this frame");
      }
      lastOrder.clear();
      discard();
      return;
    }

    // The next frame starts its barriers from this access state.
    for (auto &[storage, access] : pendingAccess) {
      storage->access = access;
    }

    // The storage of each output becomes the current storage, and the history
    // of a temporal resource.
    for (auto &[key, mapping] : mappings) {
      if (!mapping.produced) {
        continue;
      }

      mapping.definition->current = mapping.current;
      if (mapping.definition->temporal) {
        mapping.definition->history = mapping.current;
      }
    }

    lastOrder.assign(schedule.tasks.begin(), schedule.tasks.end());
    discard();
  }


  void RenderGraph::discard() {
    schedule = {};
    roots.clear();
    views.clear();
    mappings.clear();
    external.clear();
    pendingAccess.clear();
    submission = nullptr;
    phase = Phase::Update;
  }


  void RenderGraph::beginFrame() {
    requireUpdate();
    std::erase_if(registry, [](const auto &entry) { return entry.expired(); });
    std::erase_if(producers, [](const auto &entry) { return entry.second.expired(); });
  }
}  // namespace ren
