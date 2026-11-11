#pragma once
#include <ren/renderer/CommandEncoder.h>
#include <ren/renderer/graph/Resource.h>
#include <ren/renderer/FrameGlobals.h>
#include <ren/renderer/TextureHandle.h>

namespace ren {
  class RenderGraph;
  class Renderer;


  // The CPU settings of one view: its camera and render size. The owner (e.g.
  // a Viewport) can change it at any time. The graph captures it once per
  // frame, before tasks prepare, so a change applies to the next frame.
  struct ViewContext {
    glm::uvec2 renderSize{1};  // In pixels. The capture clamps each dimension to at least one.
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
  };


  // A copy of a ViewContext for one frame. Preparation and recording read this
  // copy, not the live ViewContext.
  struct CapturedView {
    ViewContext values;
    FrameGlobals globals;                         // The shader globals made from values.
    VkDescriptorSet globalsSet = VK_NULL_HANDLE;  // Binds globals. Null in tests without Vulkan.
  };


  // Gives a task the storage of its handles in this frame. It cannot change
  // the graph.
  class ResourceResolver {
   public:
    explicit ResourceResolver(const RenderGraph &graph)
        : source(graph) {}


    // Return the storage of a handle in this frame, or null for history that
    // is not available.
    ref<GraphStorage> storage(GraphHandle) const;

    // Return the image of a handle, or null for history that is not available.
    ImageRef getImage(GraphHandle) const;

    // Return the bindless handle of an image, for shaders that sample it. The
    // first call for an image adds it to the bindless heap. Throws for history
    // that is not available.
    TextureHandle texture(GraphHandle) const;

    // Return the storage of a handle as type T. Throws if the storage is null
    // or of a different type.
    template <class T>
    ref<T> get(GraphHandle handle) const {
      auto value = storage(handle);
      if (!value) {
        throw std::runtime_error("Graph storage is not available");
      }

      auto result = std::dynamic_pointer_cast<T>(value);
      if (!result) {
        throw std::runtime_error("Graph storage type does not match");
      }
      return result;
    }


   private:
    const RenderGraph &source;
  };


  // What a task gets in prepareFrame().
  struct PrepareContext {
    const ResourceResolver resources;
    const CapturedView *view;    // The captured view of the task, or null if the task has no view.
    Renderer *renderer;          // Null in tests without Vulkan.
    SubmissionUnit *submission;  // Null in tests without Vulkan.

    // Keep an object until the GPU completes this frame, e.g. a framebuffer
    // that the recorded commands use. Does nothing without a submission.
    template <class T>
    void retain(ref<T> value);
  };


  // What a task gets in run(). It can record commands, but it cannot change
  // the graph.
  struct RecordContext {
    const ResourceResolver resources;
    const CapturedView *view;  // As in PrepareContext.
    CommandEncoder &encoder;   // The main encoder of the frame. Pipelines bound with it use the FrameGlobals of the view.
  };


  // What a RenderPassTask gets in run(GraphRenderPassContext &).
  struct GraphRenderPassContext {
    const ResourceResolver resources;
    const CapturedView *view;
    RenderPassEncoder &encoder;  // Records into the active render pass.
  };
}  // namespace ren
