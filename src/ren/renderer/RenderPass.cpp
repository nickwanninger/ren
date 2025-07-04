#include <ren/renderer/RenderPass.h>
#include <ren/renderer/Vulkan.h>
#include <ren/assets/Vertex.h>
#include <ren/misc/hash.h>

namespace ren {

  static std::unordered_set<RenderPass *> allRenderPasses;
  VkAttachmentDescription &RenderPass::Description::addColorAttachment(const std::string &name,
                                                                       VkFormat format) {
    VkAttachmentDescription attachment{};
    attachment.format = format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    attachments.push_back(attachment);
    attachmentNames.push_back(name);
    return attachments.back();
  }

  VkAttachmentDescription &RenderPass::Description::addDepthAttachment(const std::string &name) {
    VkAttachmentDescription attachment{};
    attachment.format = getVulkan().findDepthFormat();
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    attachments.push_back(attachment);
    attachmentNames.push_back(name);
    return attachments.back();
  }



  json RenderPass::Description::serialize(void) const {
    // Serialize the render pass description to JSON.
    json j;
    j["name"] = name;
    j["attachments"] = json::array();
    for (size_t i = 0; i < attachments.size(); ++i) {
      auto &attachment = attachments[i];
      json attachmentJson;
      attachmentJson["name"] = attachmentNames[i];
      attachmentJson["format"] = (u32)attachment.format;
      attachmentJson["loadOp"] = (u32)attachment.loadOp;
      attachmentJson["storeOp"] = (u32)attachment.storeOp;
      attachmentJson["initialLayout"] = (u32)attachment.initialLayout;
      attachmentJson["finalLayout"] = (u32)attachment.finalLayout;
      j["attachments"].push_back(attachmentJson);
    }
    return j;
  }


  // ------------------------------------------- //

  size_t RenderPass::Description::hash(void) const {
    u64 hash = 0;
    // Create a hash from the name and the attachments.
    ren::hashStd(hash, name);  // hash the name of the render pass.
    for (u64 i = 0; i < attachments.size(); ++i) {
      ren::hashStd(hash, attachmentNames[i]);
      auto &attachment = attachments[i];

      // There are no pointers in the attachment, it's just a description, so we
      // can hash its bytes directly.
      ren::hash(hash, attachment);
    }

    return hash;
  }


  RenderPass::RenderPass(Description &desc)
      : desc(desc) {
    allRenderPasses.insert(this);
    renderPass = VK_NULL_HANDLE;

    build();
  }

  RenderPass::~RenderPass(void) {
    allRenderPasses.erase(this);
    // Call the cleanup function to release resources
    cleanup();
  }

  const std::unordered_set<RenderPass *> RenderPass::allPasses(void) { return allRenderPasses; }



  void RenderPass::cleanup(void) {
    // If the render pass is not null, destroy it
    if (renderPass != VK_NULL_HANDLE) {
      vkDestroyRenderPass(getVulkan().device, renderPass, nullptr);
      renderPass = VK_NULL_HANDLE;
    }
  }


  ref<RenderTarget> RenderPass::createRenderTarget(u32 width, u32 height) {
    // Create a render target with the given width and height.
    RenderTargetDescription rtDesc;
    rtDesc.attachments.clear();


    // Add all the attachments from the render pass description.
    for (int i = 0; i < this->desc.attachments.size(); ++i) {
      auto &attachment = this->desc.attachments[i];
      auto &name = this->desc.attachmentNames[i];

      // Allocate a image view for this attachment.
      // We will use the same format as the attachment.


      if (attachment.finalLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        // This is a depth attachment.
        auto image = ImageBuilder(name)
                         .setWidth(width)
                         .setHeight(height)
                         .setFormat(attachment.format)
                         .setViewAspectMask(VK_IMAGE_ASPECT_DEPTH_BIT)
                         .setUsage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                   VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
                         .build();

        rtDesc.attachments.push_back(RenderTargetAttachment(RenderTargetAttachmentTypeDepth, image,
                                                            attachment.format, name));
      } else {
        // This is a color attachment.
        auto image = ImageBuilder(name)
                         .setWidth(width)
                         .setHeight(height)
                         .setFormat(attachment.format)
                         .setViewAspectMask(VK_IMAGE_ASPECT_COLOR_BIT)
                         .setUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                   VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
                         .build();

        rtDesc.attachments.push_back(RenderTargetAttachment(RenderTargetAttachmentTypeColor, image,
                                                            attachment.format, name));
      }
    }

    // Create the render target with the render pass reference.
    return makeRef<RenderTarget>(rtDesc);
  }

  void RenderPass::build(void) {
    auto &vulkan = getVulkan();


    // Now, go through and make references for the attachments.
    // We can have many color attachments, but only one depth attachment.
    std::vector<VkAttachmentReference> colorRefs;
    VkAttachmentReference depthRef;
    bool hasDepth = false;
    // Loop through the attachments and create references.
    for (size_t i = 0; i < desc.attachments.size(); ++i) {
      auto &attachment = desc.attachments[i];

      if (attachment.finalLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        // This is a depth attachment.
        depthRef.attachment = i;
        depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        hasDepth = true;
        continue;
      } else {
        VkAttachmentReference ref{};
        ref.attachment = i;
        ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        colorRefs.push_back(ref);
      }
    }

    // TODO: Handle all this.
    VkSubpassDescription subpass{};

    fmt::println("attachments: {}", desc.attachments.size());
    fmt::println("RenderPass: Creating subpass with {} color references. Depth = {}",
                 colorRefs.size(), hasDepth);
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = colorRefs.size();
    subpass.pColorAttachments = colorRefs.data();
    subpass.pDepthStencilAttachment = hasDepth ? &depthRef : nullptr;


    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcAccessMask = 0;
    dependency.srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(desc.attachments.size());
    renderPassInfo.pAttachments = desc.attachments.data();

    // Simply build
    if (vkCreateRenderPass(vulkan.device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
      throw std::runtime_error("failed to create render pass!");
    }
  }
}  // namespace ren
