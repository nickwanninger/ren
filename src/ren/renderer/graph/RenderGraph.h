#pragma once
#include <ren/renderer/graph/Task.h>
#include <ren/renderer/graph/Schedule.h>
#include <ren/renderer/graph/ImageResource.h>
#include <ren/renderer/submission/SubmissionUnit.h>
#include <unordered_map>

namespace ren {


  class GraphScope;


  // A RenderGraph holds the rendering tasks of the renderer and the resources
  // that they use. The Renderer owns one graph. Systems add tasks to it
  // through GraphScopes.
  //
  // Tasks declare the resources that they read, write, and update. The graph
  // finds the dependencies from these declarations. A task runs only in a
  // frame where it is enqueued, or where an enqueued task needs its output.
  //
  // Each frame goes through these steps, in this order:
  //   1. Update:  create or remove tasks, and enqueue work.
  //   2. Prepare: prepareFrame() selects and orders the tasks, captures each
  //               view, assigns storage to each resource, and calls
  //               prepareFrame() on each task.
  //   3. Display: code outside the graph (e.g. ImGui) can get prepared
  //               images with displayImage().
  //   4. Record:  record() adds barriers and records each task into the
  //               command buffer of the frame.
  //   5. Submit:  after the frame is submitted, submitted() saves the new
  //               resource state and history. If an error occurs, discard()
  //               drops the work of the frame.
  //
  // Tasks and dependencies can change only in the update step. Other calls
  // throw if they occur in the wrong step.
  class RenderGraph {
   public:
    RenderGraph() = default;
    RenderGraph(const RenderGraph &) = delete;
    RenderGraph &operator=(const RenderGraph &) = delete;


    // A resource is the definition of data that tasks share, e.g. an image
    // with a size and a format. A GraphHandle refers to one version of a
    // resource: its contents after a specified task writes or updates it.
    // Storage is the actual allocation. prepareFrame() assigns storage to each
    // resource, so a resource can use different storage in different frames.

    // Register a custom resource and return a handle to its first version.
    // The first task that writes this handle becomes its producer. A resource
    // can belong to only one graph.
    GraphHandle createResource(std::string_view name, ref<GraphResource>);

    // Make an image resource. The initial access sets the type and usage of
    // the image, e.g. DepthTarget makes a depth image.
    GraphHandle createImage(std::string_view name, const GraphImageSpec &, GraphAccess initial);

    // Return a handle to the history of a temporal resource. The history is the
    // storage from the last frame where the producer ran, which is not always
    // the previous frame. It contains the final contents of that frame, after
    // all updates. A read of this handle does not make the producer run. The
    // history is null on first use and after a size change. Throws for a
    // version between two updates.
    GraphHandle previous(GraphHandle) const;

    // Return the resource of a handle. Throws if the handle is from a
    // different graph.
    ref<GraphResource> getResource(GraphHandle) const;

    // Return the resource of a handle as type T. Throws if the type is
    // different.
    template <class T>
    ref<T> get(GraphHandle handle) const {
      // TODO: dynamic_pointer_cast can be slow on the critical path.
      auto value = std::dynamic_pointer_cast<T>(getResource(handle));
      if (!value) {
        throw std::runtime_error("Graph resource type does not match");
      }
      return value;
    }

    // Remove the entries of tasks that were destroyed since the last frame.
    // Call at the start of each frame.
    void beginFrame();

    // Make a task run in this frame. The producers of its inputs also run.
    // A task runs once, even if it is enqueued more than once. The task must
    // be from a GraphScope of this graph. The graph keeps the task until
    // submitted() or discard().
    void enqueue(RenderTask &);

    // Select, order, and prepare the tasks of this frame (step 2). Tasks
    // prepare in dependency order, so the storage of each producer is ready
    // before its consumers prepare. The submission keeps the tasks, captured
    // views, and storage until the GPU completes the frame. If an error
    // occurs, the frame is discarded and the error is thrown again. The
    // arguments are null only in tests without Vulkan.
    void prepareFrame(Renderer *renderer = nullptr, SubmissionUnit *submission = nullptr);

    // Return the storage of a handle in this frame. Use it during or after
    // prepareFrame(). For a resource that is not in this frame, return the
    // storage from the last frame where it ran. Return null for history that
    // is not available.
    ref<GraphStorage> resolve(GraphHandle) const;

    // Return an image for use outside the graph, e.g. by ImGui (step 3). Call
    // after prepareFrame() and before record(). The image changes to a
    // shader-read layout at the end of the graph work, and the frame keeps it
    // until the GPU completes. This does not make the producer run. Return
    // null for history that is not available. Throws if a current image has
    // no storage.
    ref<ImageStorage> displayImage(GraphHandle);

    // Add barriers and record each task into the command buffer of the
    // current frame (step 4). Each task gets the FrameGlobals of its view.
    void record();

    // Save the resource state and history of this frame (step 5). Call after
    // the frame is submitted to the queue. Then the graph returns to the
    // update step.
    void submitted();

    // Drop the work of this frame and return to the update step. This does
    // not save resource state or history.
    void discard();

    // Show the registered tasks in ImGui.
    void inspect();

    // These steps are separate for tests. prepareFrame() and record() call
    // them.

    // Select the enqueued tasks and the producers that they need, and put them
    // in dependency order. Throws for a missing producer or a dependency cycle.
    //
    // TODO: After this call, the dependencies of the selected tasks cannot
    // change, even if the frame is discarded. Must a task be replaced to change
    // its dependencies, or must a task unseal at the end of each frame?
    RenderSchedule compile();

    // Make the barrier list for the prepared frame.
    const RenderSchedule &compileTransitions();

   private:
    // Throws if the graph is not in the update step.
    void requireUpdate() const;

    // Throws if the task cannot get new declarations: the graph is not in the
    // update step, the task is sealed, or the task is from a different graph.
    void requireDeclarable(const RenderTask &) const;

    // The step of the current frame. Each public call checks it.
    enum class Phase {
      Update,    // Tasks, resources, and dependencies can change. Tasks can be enqueued.
      Select,    // prepareFrame() selects and orders tasks and captures views.
      Prepare,   // Tasks prepare. resolve() is available.
      Prepared,  // Storage is fixed. displayImage() is available.
      Compiled,  // The barrier list is complete. record() is available.
      Record,    // Tasks record commands.
      Recorded,  // Recording is complete. submitted() is available.
    };
    Phase phase = Phase::Update;

    // Handles and resources keep a weak reference to this token. A handle with
    // a different token is from a different graph.
    ref<void> identity = std::make_shared<int>(0);

    // IDs are never used again, so an ID cannot refer to a different task
    // after a task is destroyed.
    u64 nextTask = 1, nextVersion = 1;

    // All registered tasks, in creation order. The schedule uses this order
    // when it can choose between tasks. GraphScopes own the tasks.
    std::vector<std::weak_ptr<RenderTask>> registry;

    // Task ID to task. A version keeps the ID of its producer, not the task,
    // so a handle does not keep its producer alive.
    std::unordered_map<u64, std::weak_ptr<RenderTask>> producers;

    // The rest of the state is for the current frame. discard() clears it.

    std::vector<ref<RenderTask>> roots;  // The enqueued tasks.
    RenderSchedule schedule;             // The ordered tasks, then the barriers and tasks to record.

    // One capture per ViewContext. The captured values and FrameGlobals do not
    // change if the ViewContext changes after capture.
    std::unordered_map<const ViewContext *, ref<CapturedView>> views;

    // The storage of one resource in this frame.
    struct Mapping {
      ref<GraphResource> definition;
      ref<GraphStorage> current;   // Storage for the versions in this frame.
      ref<GraphStorage> previous;  // The history, or null.
      bool produced = false;       // A task writes the resource in this frame, so submitted() saves it.
    };
    std::unordered_map<GraphResource *, Mapping> mappings;

    // Images from displayImage(). They change to a shader-read layout at the
    // end of the graph work.
    std::vector<ref<GraphStorage>> external;

    // The access state of each storage after the barriers of this frame.
    // submitted() saves it in the storage.
    std::unordered_map<GraphStorage *, std::vector<GraphAccess>> pendingAccess;

    SubmissionUnit *submission = nullptr;  // Null in tests without Vulkan.

    // Throws if the handle is null or from a different graph.
    const GraphVersion &validate(GraphHandle) const;

    // Choose the storage for the output of a resource in this frame: reuse a
    // compatible allocation, or allocate a new one.
    static ref<GraphStorage> assignStorage(GraphResource &, const CapturedView *);

    // RenderTask::read(), write(), and updates() call these. An update makes a
    // new version of the resource and returns its handle.
    void declareRead(RenderTask &, GraphHandle, GraphAccess);
    void declareWrite(RenderTask &, GraphHandle, GraphAccess);
    GraphHandle declareUpdate(RenderTask &, GraphHandle, GraphAccess);

    // Add a barrier from the last access of the storage to this access. Reads
    // accumulate until the next write.
    void appendAccess(ref<GraphStorage>, GraphAccess);
    friend class RenderTask;
    friend class GraphScope;
  };




  // A GraphScope owns a group of tasks in a RenderGraph, e.g. the tasks of one
  // viewport. A scope is the only way to add tasks to the graph. It does not
  // change how tasks are scheduled: a task can use outputs from tasks in other
  // scopes, and a shared producer runs once per frame.
  //
  // If the scope is destroyed or cleared, its tasks are removed. A frame in
  // flight keeps its tasks until the GPU completes it. If a task in a different
  // scope needs an output of a removed task, prepareFrame() throws.
  class GraphScope {
   public:
    // All tasks of this scope use this view, which can be null. Tasks without
    // a view cannot use view-relative image sizes.
    explicit GraphScope(RenderGraph &graph, ref<ViewContext> view = {})
        : owner(graph)
        , view(std::move(view)) {}

    GraphScope(const GraphScope &) = delete;
    GraphScope &operator=(const GraphScope &) = delete;


    // Make a task of type T and add it to the graph. T is made with the graph
    // and then args. The task gets the view of this scope. The reference is
    // valid while the scope keeps the task.
    template <class T, class... Args>
    T &addTask(const char *name, Args &&...args) {
      owner.requireUpdate();
      auto task = make<T>(owner, std::forward<Args>(args)...);
      task->setName(name);
      task->view = view;
      tasks.push_back(task);
      owner.registry.push_back(task);
      owner.producers.emplace(task->id, task);
      return *task;
    }

    // Return a builder for a task that runs lambdas. The builder adds the task
    // when you call execute() or render().
    TaskBuilder pass(const char *name) { return TaskBuilder(*this, name); }

    // Enqueue all tasks of this scope for this frame.
    void enqueue() {
      for (auto &task : tasks) {
        owner.enqueue(*task);
      }
    }

    RenderGraph &graph() const { return owner; }

    // Remove all tasks of this scope. Use it to rebuild the tasks.
    void clear() {
      owner.requireUpdate();
      tasks.clear();
    }

   private:
    RenderGraph &owner;                  // The graph that contains the tasks of this scope.
    ref<ViewContext> view;               // The camera, size, and FrameGlobals of the tasks of this scope.
    std::vector<ref<RenderTask>> tasks;  // The tasks of this scope. The graph keeps only weak references.
  };


  template <class T>
  void PrepareContext::retain(ref<T> value) {
    if (submission) {
      submission->retain(std::move(value));
    }
  }
}  // namespace ren
