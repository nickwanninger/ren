#pragma once

#include <ren/types.h>
#include <vulkan/vulkan_core.h>
#include <ren/renderer/RenderTarget.h>
#include <ren/core/UUID.h>
#include <ren/renderer/Vulkan.h>

namespace ren {



  // A RenderPass is a description of how render passes should be ordered
  // and what attachments they should use.
  class RenderPass : public std::enable_shared_from_this<RenderPass>, public ren::HasUUID, public ren::VulkanResource {
   public:
    struct Description {
      std::string name = "pass";
      // The attachments used in this render pass.
      // You can either manually specify the attachments,
      // or add them with the below method:
      // TODO: Make an AttachmentDescription to wrap this with extra data.
      std::vector<VkAttachmentDescription> attachments;
      std::vector<std::string> attachmentNames;  // Names of the attachments for debugging.

      u32 colorAttachments = 0;
      u32 depthAttachments = 0;  // We can only have one depth attachment, but we'll just count it.

      VkAttachmentDescription &addColorAttachment(const std::string &name,
                                                  VkFormat format = VK_FORMAT_B8G8R8A8_SRGB);
      VkAttachmentDescription &addDepthAttachment(const std::string &name = "depth");

      // a hash function for this description
      size_t hash(void) const;


      json serialize(void) const;
    };


    static const std::unordered_set<RenderPass *> allPasses(void);


    RenderPass(Description &desc);
    ~RenderPass();



    const std::string &getName() const { return desc.name; }
    VkRenderPass getHandle(void) const { return renderPass; }

    const Description &getDescription(void) const { return desc; }

    // Allocate a render target with the given name and dimensions.
    ref<RenderTarget> createRenderTarget(u32 width, u32 height);

   private:
    void build(void);    // Internal: build the render pass.
    void cleanup(void);  // Internal: cleanup the render pass.
    VkRenderPass renderPass = VK_NULL_HANDLE;

    Description desc;
  };

}  // namespace ren