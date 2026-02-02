#include <ren/renderer/graph/RenderPassTask.h>
#include <ren/renderer/graph/RenderGraph.h>
#include <ren/renderer/graph/ImageResource.h>
#include <ren/renderer/Renderer.h>
#include "ren/renderer/graph/RunContext.h"

namespace ren {

  RenderPassTask::RenderPassTask(RenderGraph &graph)
      : RenderTask(graph)
      , pass(nullptr)
      , target(nullptr) {}

  GraphHandle RenderPassTask::addColorAttachment(const std::string_view &name, const GraphImageSpec &spec) {
    RenderGraph &graph = this->graph();
    GraphHandle handle = graph.createImage(name, spec, GraphAccess::RenderTarget);
    this->write(handle, GraphAccess::RenderTarget);

    // Store the attachment handle for later construction of RenderTarget
    this->attachmentHandles.emplace_back(std::string(name), handle);

    // Update the render pass description for VkRenderPass creation
    this->desc.addColorAttachment(name, spec.format);
    return handle;
  }

  GraphHandle RenderPassTask::addDepthAttachment(const std::string_view &name, const GraphImageSpec &uspec) {
    GraphImageSpec spec = uspec;  // Copy to allow format modification

    RenderGraph &graph = this->graph();
    spec.format = getVulkan().findDepthFormat();
    GraphHandle handle = graph.createImage(name, spec, GraphAccess::DepthTarget);
    this->write(handle, GraphAccess::DepthTarget);

    // Store the attachment handle for later construction of RenderTarget
    this->attachmentHandles.emplace_back(std::string(name), handle);

    // Update the render pass description for VkRenderPass creation
    this->desc.addDepthAttachment(name);
    return handle;
  }

  void RenderPassTask::prepare() {
    REN_PROFILE_FUNCTION();

    // Step 1: Get or create the cached VkRenderPass from the description
    // This uses hash-based deduplication, so multiple tasks with identical
    // attachments share the same VkRenderPass (zero-cost abstraction).

    // Step 2: Build RenderTargetDescription from graph image resources
    // We iterate through our stored attachment handles and fetch the actual
    // Image objects from the render graph.
    RenderTargetDescription targetDesc;

    for (const auto &[name, handle] : this->attachmentHandles) {
      // Get the image resource from the graph
      ren::ImageRef image = graph().getImage(handle);

      // Find the write access for this attachment from this task's results
      GraphAccess writeAccess = GraphAccess::RenderTarget;  // Default
      for (const auto &result : getResults()) {
        if (result.handle == handle) {
          writeAccess = result.access;
          break;
        }
      }

      RenderTargetAttachmentType attachmentType =
          (writeAccess == GraphAccess::DepthTarget) ? RenderTargetAttachmentTypeDepth : RenderTargetAttachmentTypeColor;

      // Add attachment to the description
      targetDesc.attachments.push_back(RenderTargetAttachment(attachmentType, image, image->getFormat(), name));
    }

    Renderer &renderer = Renderer::get();
    this->pass = renderer.getRenderPassCache().get(desc);
    // Step 3: Create the RenderTarget
    // The RenderTarget stores attachments and manages VkFramebuffer caching
    // (framebuffers are cached per RenderPass UUID for efficiency).
    target = make<RenderTarget>(targetDesc);
  }

  void RenderPassTask::unprepare() {
    // Release the render pass and target references.
    // The RenderPass is cached globally and may be reused by other tasks,
    // so we just release our reference. The target is task-local and will
    // be rebuilt in the next prepare() if needed.
    pass.reset();
    target.reset();
  }

  void RenderPassTask::run(GraphRunContext &ctx) {
    auto timestampTicket = ctx.encoder.beginTimestampQuery(this->name().c_str());
    auto rpe = ctx.encoder.beginRenderPass(*pass, *target);
    GraphRenderPassContext rctx(ctx.graph, ctx.renderer, rpe);
    this->run(rctx);
    rpe.end();
    ctx.encoder.endTimestampQuery(timestampTicket);
  }

}  // namespace ren
