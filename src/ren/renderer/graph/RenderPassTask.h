#pragma once
#include <ren/renderer/graph/Task.h>
#include <ren/renderer/RenderPass.h>
#include <ren/renderer/RenderTarget.h>

namespace ren {


  // A RenderPassTask is a task that draws into one Vulkan render pass. The task
  // declares its attachments when it is built. Each frame, prepareFrame()
  // makes the render pass and framebuffer from the prepared storage, and the
  // derived class records draws with run(GraphRenderPassContext &).
  class RenderPassTask : public RenderTask {
   public:
    explicit RenderPassTask(RenderGraph &graph)
        : RenderTask(graph) {}


    // Make a new image and write it as an attachment of this pass. The task
    // becomes the producer of the image. A depth attachment always uses the
    // depth format of the device; the format in spec is ignored.
    GraphHandle addColorAttachment(std::string_view name, const GraphImageSpec &spec);
    GraphHandle addDepthAttachment(std::string_view name, const GraphImageSpec &spec);

    // Draw on top of the contents of an existing image, and return its new
    // version. The pass loads the old contents. Throws if a color update gets
    // a depth image, or a depth update gets a color image.
    GraphHandle updateColorAttachment(std::string_view name, GraphHandle image);
    GraphHandle updateDepthAttachment(std::string_view name, GraphHandle image);


    // Make the render pass and the framebuffer from this frame's storage.
    // Throws if the attachments are different sizes. A derived class that
    // overrides this must call it.
    void prepareFrame(PrepareContext &) override;

    // Record the draws of this pass. The render pass is active.
    virtual void run(GraphRenderPassContext &) const = 0;


   protected:
    // Begin the render pass, call run(GraphRenderPassContext &), and end it.
    void run(RecordContext &) const override final;

    // The render pass and framebuffer of the current frame. Valid after prepareFrame().
    ref<RenderPass> getRenderPass() const { return pass; }
    ref<RenderTarget> getRenderTarget() const { return target; }


   private:
    // Add an attachment to the render pass description, with the layouts that
    // the graph barriers use.
    VkAttachmentDescription &describeAttachment(std::string_view name, VkFormat format, bool depth);
    GraphHandle attachment(std::string_view name, const GraphImageSpec &spec, bool depth);
    GraphHandle updateAttachment(std::string_view name, GraphHandle image, bool depth);

    RenderPass::Description desc;  // The attachments of the render pass, in declaration order.
    ref<RenderPass> pass;          // The render pass of the current frame.
    ref<RenderTarget> target;      // The framebuffer of the current frame.

    // The name and handle of each attachment, in the same order as desc.
    std::vector<std::pair<std::string, GraphHandle>> attachments;
  };
}  // namespace ren
