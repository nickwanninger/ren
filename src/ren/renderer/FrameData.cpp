#include <ren/renderer/FrameData.h>
#include <ren/renderer/Vulkan.h>
#include <ren/renderer/Swapchain.h>
#include <ren/core/Application.h>
#include <fmt/core.h>


namespace ren {
  FrameData::FrameData(u32 frameIndex, Swapchain &sc, VkImage swapchainImage,
                       VkImageView swapchainImageView) {
    auto &app = ren::Application::get();
    this->frameIndex = frameIndex;
    auto &vulkan = ren::getVulkan();
    // ---- Add the swapchain image to the deviceImage in the framedata ---- //
    VkImageCreateInfo imageCreateInfo = {};  // Just so the ren::Image class can have it.
    imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = sc.imageFormat;
    imageCreateInfo.extent.width = sc.deviceExtent.width;
    imageCreateInfo.extent.height = sc.deviceExtent.height;
    imageCreateInfo.extent.depth = 1;

    this->deviceImage = ren::Image::create(fmt::format("device #{}", frameIndex), swapchainImage,
                                           swapchainImageView,
                                           VK_NULL_HANDLE,  // Null allocation is a little strange.
                                           imageCreateInfo);

    this->depthImage = ren::ImageBuilder(fmt::format("depth #{}", frameIndex))
                           .setWidth(sc.deviceExtent.width)
                           .setHeight(sc.deviceExtent.height)
                           .setFormat(sc.depthFormat)
                           .setUsage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
                           .setViewAspectMask(VK_IMAGE_ASPECT_DEPTH_BIT)
                           .build();

    // allocate the framebuffer
    VkFramebufferCreateInfo deviceFramebufferCreate{};

    std::array<VkImageView, 2> attachments = {this->deviceImage->getImageView(),
                                              this->depthImage->getImageView()};

    VkImageView deviceImageView = this->deviceImage->getImageView();
    deviceFramebufferCreate.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    deviceFramebufferCreate.renderPass = ren::Renderer::get().getRenderPass().getHandle();
    deviceFramebufferCreate.attachmentCount = attachments.size();
    deviceFramebufferCreate.pAttachments = attachments.data();
    deviceFramebufferCreate.width = sc.deviceExtent.width;
    deviceFramebufferCreate.height = sc.deviceExtent.height;
    deviceFramebufferCreate.layers = 1;
    if (vkCreateFramebuffer(vulkan.device, &deviceFramebufferCreate, nullptr,
                            &this->deviceFramebuffer) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create device framebuffer");
    }

    // ---- Allocate render targets ---- //

    // Allocate the render image.
    // this->renderImage = ren::ImageBuilder(fmt::format("render #{}", frameIndex))
    //                         .setWidth(sc.renderExtent.width)
    //                         .setHeight(sc.renderExtent.height)
    //                         .setFormat(sc.imageFormat)
    //                         .setInitialLayout(VK_IMAGE_LAYOUT_UNDEFINED)
    //                         .setUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
    //                                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
    //                                   VK_IMAGE_USAGE_SAMPLED_BIT)
    //                         .setViewAspectMask(VK_IMAGE_ASPECT_COLOR_BIT)
    //                         .build();

    // this->renderTexture = makeRef<ren::Texture>(this->renderImage);
    // this->depthTexture = makeRef<ren::Texture>(this->depthImage);


    // std::array<VkImageView, 2> attachments = {this->renderImage->getImageView(),
    //                                           this->depthImage->getImageView()};

    // VkFramebufferCreateInfo framebufferInfo{};
    // framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    // framebufferInfo.renderPass = vulkan.renderPass->getHandle();
    // framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    // framebufferInfo.pAttachments = attachments.data();
    // framebufferInfo.width = sc.renderExtent.width;
    // framebufferInfo.height = sc.renderExtent.height;
    // framebufferInfo.layers = 1;

    // if (vkCreateFramebuffer(vulkan.device, &framebufferInfo, nullptr, &this->renderFramebuffer)
    // !=
    //     VK_SUCCESS) {
    //   throw std::runtime_error("failed to create framebuffer!");
    // }

    // ---- Allocate the semaphores and fence for this frame ---- //
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VK_CHECK(
        vkCreateSemaphore(vulkan.device, &semaphoreInfo, nullptr, &this->imageAvailableSemaphore));
    VK_CHECK(
        vkCreateSemaphore(vulkan.device, &semaphoreInfo, nullptr, &this->renderFinishedSemaphore));

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // Start signaled
    VK_CHECK(vkCreateFence(vulkan.device, &fenceInfo, nullptr, &this->inFlightFence));


    // ---- Allocate the command buffer for this frame ---- //
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = vulkan.commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(vulkan.device, &allocInfo, &this->commandBuffer);
  }


  FrameData::~FrameData() {
    auto &vulkan = ren::getVulkan();

    // This needs to be done because the ImageView is not managed by the Swapchain
    vkDestroyImageView(vulkan.device, this->deviceImage->getImageView(), nullptr);
    this->renderImage.reset();
    this->depthImage.reset();
    this->deviceImage.reset();
    vkDestroyFramebuffer(vulkan.device, this->renderFramebuffer, nullptr);
    vkDestroyFramebuffer(vulkan.device, this->deviceFramebuffer, nullptr);
    vkDestroySemaphore(vulkan.device, this->imageAvailableSemaphore, nullptr);
    vkDestroySemaphore(vulkan.device, this->renderFinishedSemaphore, nullptr);
    vkDestroyFence(vulkan.device, this->inFlightFence, nullptr);
  }
}  // namespace ren