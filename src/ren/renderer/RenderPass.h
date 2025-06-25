#pragma once

#include <ren/types.h>
#include <vulkan/vulkan_core.h>
#include <ren/renderer/RenderTarget.h>


namespace ren {


  // A RenderPass is a description of how render passes should be ordered
  // and what attachments they should use.
  class RenderPass {
   public:
    struct Description {
      // The attachments used in this render pass.
      // You can either manually specify the attachments,
      // or add them with the below method:
      std::vector<VkAttachmentDescription> attachments;

      VkAttachmentDescription &addColor(VkFormat format = VK_FORMAT_B8G8R8A8_SRGB);
      VkAttachmentDescription &addDepth();
    };


    RenderPass(Description &desc);
    ~RenderPass();



    VkRenderPass getHandle(void) const { return renderPass; }

    const Description &getDescription(void) const { return desc; }

   private:
    void build(void);    // Internal: build the render pass.
    void cleanup(void);  // Internal: cleanup the render pass.
    VkRenderPass renderPass = VK_NULL_HANDLE;

    Description desc;
  };

}  // namespace ren