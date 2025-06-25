#include <ren/renderer/RenderTarget.h>
#include <ren/renderer/Vulkan.h>

namespace ren {

  void RenderTargetDescription::setupColor(ImageRef texture, VkFormat format) {
    attachments.clear();
    attachments.push_back(RenderTargetAttachment(RenderTargetAttachmentTypeColor, texture, format));
  }

  void RenderTargetDescription::setupColorAndDepth(ImageRef colorImage, VkFormat colorFormat,
                                                   ImageRef depthImage, VkFormat depthFormat) {
    attachments.clear();

    auto color = RenderTargetAttachment(RenderTargetAttachmentTypeColor, colorImage, colorFormat);
    attachments.push_back(color);

    auto depth = RenderTargetAttachment(RenderTargetAttachmentTypeDepth, depthImage, depthFormat);
    attachments.push_back(depth);
  }


  RenderTarget::RenderTarget(const RenderTargetDescription &desc, ref<RenderPass> renderPass)
      : attachments(desc.attachments)
      , renderPass(renderPass)  // copy attachments
  {
    // Construct the Framebuffer from the attachments.
    auto &vulkan = getVulkan();
    std::vector<VkImageView> attachmentViews;
    attachmentViews.reserve(attachments.size());
    this->width = 0;
    this->height = 0;
    for (const auto &attachment : attachments) {
      auto twidth = attachment.texture->getWidth();
      auto theight = attachment.texture->getHeight();

      // Validate that all attachments have the same dimensions
      if (this->width == 0 && this->height == 0) {
        this->width = twidth;
        this->height = theight;
      } else if (this->width != twidth || this->height != theight) {
        fmt::print("RenderTarget: All attachments must have the same dimensions.\n");
        abort();
      }
      attachmentViews.push_back(attachment.texture->getImageView());
    }

    VkFramebufferCreateInfo framebufferInfo = {};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    // TODO: I really don't like pulling the render pass from the renderer like this.
    framebufferInfo.renderPass = renderPass->getHandle();
    framebufferInfo.attachmentCount = static_cast<u32>(attachmentViews.size());
    framebufferInfo.pAttachments = attachmentViews.data();
    framebufferInfo.width = width;
    framebufferInfo.height = height;
    framebufferInfo.layers = 1;

    // Create the framebuffer.
    if (vkCreateFramebuffer(vulkan.device, &framebufferInfo, nullptr, &framebuffer) != VK_SUCCESS) {
      fmt::print("Failed to create framebuffer.\n");
      abort();
    }
  }


  RenderTarget::~RenderTarget() {
    // Destroy the framebuffer.
    vkDestroyFramebuffer(getVulkan().device, framebuffer, nullptr);
  }


}  // namespace ren