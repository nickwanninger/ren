#include <ren/renderer/graph/RenderPassTask.h>
#include <ren/renderer/graph/RenderGraph.h>
#include <ren/renderer/Renderer.h>

namespace ren {


  VkAttachmentDescription &RenderPassTask::describeAttachment(std::string_view name, VkFormat format, bool depth) {
    auto &description = depth ? desc.addDepthAttachment(name) : desc.addColorAttachment(name, format);

    // The graph moves each attachment to this layout before the pass (see
    // ImageStorage::transition()), so the pass does not change the layout.
    description.initialLayout = description.finalLayout =
        depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    return description;
  }


  GraphHandle RenderPassTask::attachment(std::string_view name, const GraphImageSpec &spec, bool depth) {
    auto access = depth ? GraphAccess::DepthTarget : GraphAccess::RenderTarget;
    auto handle = graph().createImage(name, spec, access);
    write(handle, access);

    attachments.emplace_back(name, handle);
    describeAttachment(name, spec.format, depth);
    return handle;
  }


  GraphHandle RenderPassTask::addColorAttachment(std::string_view name, const GraphImageSpec &spec) { return attachment(name, spec, false); }


  GraphHandle RenderPassTask::addDepthAttachment(std::string_view name, const GraphImageSpec &input) {
    auto spec = input;
    spec.format = getVulkan().findDepthFormat();
    return attachment(name, spec, true);
  }


  GraphHandle RenderPassTask::updateAttachment(std::string_view name, GraphHandle input, bool depth) {
    auto definition = graph().get<ImageResource>(input);
    if ((definition->initialAccess == GraphAccess::DepthTarget) != depth) {
      throw std::runtime_error("Attachment update type does not match: " + std::string(name));
    }

    auto output = updates(input, depth ? GraphAccess::DepthTarget : GraphAccess::RenderTarget);
    attachments.emplace_back(name, output);

    // An update draws on top of the old contents, so the pass loads them.
    auto &description = describeAttachment(name, definition->spec.format, depth);
    description.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    return output;
  }


  GraphHandle RenderPassTask::updateColorAttachment(std::string_view name, GraphHandle image) { return updateAttachment(name, image, false); }


  GraphHandle RenderPassTask::updateDepthAttachment(std::string_view name, GraphHandle image) { return updateAttachment(name, image, true); }


  void RenderPassTask::prepareFrame(PrepareContext &ctx) {
    // Describe the framebuffer with the storage of this frame. All attachments
    // must have the same size.
    RenderTargetDescription targetDesc;
    glm::uvec2 size{};
    for (auto &[attachmentName, handle] : attachments) {
      auto storage = ctx.resources.get<ImageStorage>(handle);
      auto image = storage->image;

      glm::uvec2 dimensions(image->getWidth(), image->getHeight());
      if (size != glm::uvec2(0) && size != dimensions) {
        throw std::runtime_error("Framebuffer attachment sizes do not match: " + name());
      }
      size = dimensions;

      auto type = storage->depth ? RenderTargetAttachmentTypeDepth : RenderTargetAttachmentTypeColor;
      targetDesc.attachments.emplace_back(type, image, image->getFormat(), attachmentName);
    }

    // The storage can change between frames, for example after a resize, so
    // the framebuffer is made again each frame.
    // TODO: Should we keep the framebuffer while the storage stays the same?
    pass = ctx.renderer->getRenderPassCache().get(desc);
    target = make<RenderTarget>(targetDesc);

    // The frame keeps the pass and the framebuffer until the GPU completes it.
    ctx.retain(pass);
    ctx.retain(target);
  }


  void RenderPassTask::run(RecordContext &ctx) const {
    auto ticket = ctx.encoder.beginTimestampQuery(name().c_str());

    auto encoder = ctx.encoder.beginRenderPass(*pass, *target);
    GraphRenderPassContext render{ctx.resources, ctx.view, encoder};
    run(render);
    encoder.end();

    ctx.encoder.endTimestampQuery(ticket);
  }
}  // namespace ren
