#pragma once

#include <ren/renderer/Image.h>

namespace ren {
  class RenderPass;



  enum RenderTargetAttachmentType {
    RenderTargetAttachmentTypeColor,
    RenderTargetAttachmentTypeDepth,
  };

  struct RenderTargetAttachment {
    RenderTargetAttachmentType type = RenderTargetAttachmentTypeColor;



    // The texture to render into.
    ren::ImageRef texture = nullptr;

    // The format of the image attachment.
    VkFormat format = VK_FORMAT_UNDEFINED;

    std::string name;

    RenderTargetAttachment(RenderTargetAttachmentType type, ImageRef texture, VkFormat format,
                           const std::string name = "attachment")
        : type(type)
        , texture(texture)
        , format(format)
        , name(name) {}
  };


  struct RenderTargetDescription {
    std::vector<RenderTargetAttachment> attachments;
    void setupColor(ImageRef texture, VkFormat format);
    void setupColorAndDepth(ImageRef colorImage, VkFormat colorFormat, ImageRef depthImage,
                            VkFormat depthFormat);
  };


  // In Vulkan, a render target is a VkFramebuffer, which is
  // a collection of attachments and dimensions.
  // In ren parlance, these attachments are images.
  class RenderTarget {
   public:
    RenderTarget(const RenderTargetDescription &desc, ref<RenderPass> renderPass);
    ~RenderTarget();

    VkFramebuffer getHandle(void) const { return framebuffer; }

    u32 getWidth(void) const { return width; }
    u32 getHeight(void) const { return height; }
    auto &getAttachments(void) { return attachments; }

   private:
    u32 width, height;
    std::vector<RenderTargetAttachment> attachments;
    ref<RenderPass> renderPass;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    //
  };

  using RenderTargetRef = ref<RenderTarget>;

}  // namespace ren