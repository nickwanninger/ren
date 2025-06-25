#include <ren/renderer/RenderPass.h>
#include <ren/renderer/Vulkan.h>
#include <ren/assets/Vertex.h>


VkAttachmentDescription &ren::RenderPass::Description::addColor(VkFormat format) {
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
  return attachments.back();
}

VkAttachmentDescription &ren::RenderPass::Description::addDepth() {
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
  return attachments.back();
}


ren::RenderPass::RenderPass(Description &desc)
    : desc(desc) {
  renderPass = VK_NULL_HANDLE;

  build();
}

ren::RenderPass::~RenderPass(void) {
  // Call the cleanup function to release resources
  cleanup();
}


void ren::RenderPass::cleanup(void) {
  // If the render pass is not null, destroy it
  if (renderPass != VK_NULL_HANDLE) {
    vkDestroyRenderPass(getVulkan().device, renderPass, nullptr);
    renderPass = VK_NULL_HANDLE;
  }
}


void ren::RenderPass::build(void) {
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

#if 0
void ren::RenderPass::populateDefaultCreateInfo(void) {


  // ---- Depth Attachment ---- //
  depthAttachment.format = vulkan.findDepthFormat();
  depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  depthAttachmentRef.attachment = 1;
  depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  // ---- Subpass Dependency ---- //
  //   TODO: What is this?
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorAttachmentRef;
  subpass.pDepthStencilAttachment = &depthAttachmentRef;

  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcAccessMask = 0;
  dependency.srcStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.dstStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.dstAccessMask =
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;


  // ---- Attachments ---- //

  attachments.clear();

  attachments.push_back(colorAttachment);
  attachments.push_back(depthAttachment);


  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.subpassCount = 1;
  renderPassInfo.pSubpasses = &subpass;
  renderPassInfo.dependencyCount = 1;
  renderPassInfo.pDependencies = &dependency;
}
#endif