#pragma once

#include <ren/types.h>
#include <vulkan/vulkan_core.h>


namespace ren {
  // This class represents a Vulkan Render pass, and is used to
  // encapsulate the render pass creation and management.
  // Subclasses can extend this class to create specific render passes.
  class RenderPass {
   public:
    RenderPass();
    ~RenderPass();

    void build();


    VkRenderPass getHandle(void) const { return renderPass; }


    VkAttachmentDescription colorAttachment{};
    VkAttachmentReference colorAttachmentRef{};

    VkAttachmentDescription depthAttachment{};
    VkAttachmentReference depthAttachmentRef{};

    VkSubpassDescription subpass{};
    VkSubpassDependency dependency{};
    VkRenderPassCreateInfo renderPassInfo{};

    std::vector<VkAttachmentDescription> attachments;

   private:
    void populateDefaultCreateInfo();
    void cleanup(void);
    VkRenderPass renderPass = VK_NULL_HANDLE;
  };

}  // namespace ren