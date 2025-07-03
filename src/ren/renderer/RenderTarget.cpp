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


  RenderTarget::RenderTarget(const RenderTargetDescription &desc)
      : attachments(desc.attachments) {}


  RenderTarget::~RenderTarget() {
    auto &vulkan = getVulkan();
    // Destroy the framebuffers in the cache
    for (auto &pair : m_cache) {
      vkDestroyFramebuffer(vulkan.device, pair.second, nullptr);
    }
  }

  VkFramebuffer RenderTarget::getHandle(RenderPass &pass) {
    auto uuid = pass.getUUID();


    // Check if the framebuffer is already cached.
    auto it = m_cache.find(uuid);
    if (it != m_cache.end()) {
      // If it is cached, return the cached framebuffer.
      return it->second;
    } else {
      // Otherwise, create a new framebuffer and cache it.
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

      VkFramebuffer framebuffer = VK_NULL_HANDLE;

      VkFramebufferCreateInfo framebufferInfo = {};
      framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      // TODO: I really don't like pulling the render pass from the renderer like this.
      framebufferInfo.renderPass = pass.getHandle();
      framebufferInfo.attachmentCount = static_cast<u32>(attachmentViews.size());
      framebufferInfo.pAttachments = attachmentViews.data();
      framebufferInfo.width = width;
      framebufferInfo.height = height;
      framebufferInfo.layers = 1;

      // Create the framebuffer.
      if (vkCreateFramebuffer(vulkan.device, &framebufferInfo, nullptr, &framebuffer) !=
          VK_SUCCESS) {
        fmt::print("Failed to create framebuffer.\n");
        abort();
      }

      // Cache the framebuffer.
      m_cache[uuid] = framebuffer;
      return framebuffer;
    }
  }


}  // namespace ren