#include <ren/renderer/submission/FrameSubmissionUnit.h>
#include <ren/renderer/Swapchain.h>
#include <ren/renderer/submission/SubmissionQueue.h>
#include <ren/renderer/Image.h>
#include <fmt/core.h>

namespace ren {

  FrameSubmissionUnit::FrameSubmissionUnit(u32 slotIndex, Swapchain &sc, VkImage swapchainImage, VkImageView swapchainImageView)
      : SubmissionUnit()
      , slotIndex(slotIndex)
      , swapchainImageIndex(slotIndex)
      , m_swapchain(sc) {
    REN_PROFILE_FUNCTION();
    auto &vulkan = ren::getVulkan();

    // Create device image wrapper for swapchain image
    VkImageCreateInfo imageCreateInfo = {};
    imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = sc.imageFormat;
    imageCreateInfo.extent.width = sc.deviceExtent.width;
    imageCreateInfo.extent.height = sc.deviceExtent.height;
    imageCreateInfo.extent.depth = 1;

    this->deviceImage =
        ren::Image::create(fmt::format("device #{}", slotIndex), swapchainImage, swapchainImageView, VK_NULL_HANDLE, imageCreateInfo);

    // Create depth image
    this->depthImage = ren::ImageBuilder(fmt::format("depth #{}", slotIndex))
                           .setWidth(sc.deviceExtent.width)
                           .setHeight(sc.deviceExtent.height)
                           .setFormat(sc.depthFormat)
                           .setUsage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
                           .setViewAspectMask(VK_IMAGE_ASPECT_DEPTH_BIT)
                           .build();

    // Create render target
    RenderTargetDescription renderTargetDesc;
    renderTargetDesc.setupColorAndDepth(this->deviceImage, sc.imageFormat, this->depthImage, sc.depthFormat);
    this->renderTarget = make<RenderTarget>(renderTargetDesc);

    // Create semaphores
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VK_CHECK(vkCreateSemaphore(vulkan.device, &semaphoreInfo, nullptr, &this->imageAvailableSemaphore));
    VK_CHECK(vkCreateSemaphore(vulkan.device, &semaphoreInfo, nullptr, &this->renderFinishedSemaphore));
  }

  FrameSubmissionUnit::~FrameSubmissionUnit() {
    auto &vulkan = ren::getVulkan();

    // Wait for any pending work before cleanup
    waitForFence();

    // Destroy semaphores
    vkDestroySemaphore(vulkan.device, this->imageAvailableSemaphore, nullptr);
    vkDestroySemaphore(vulkan.device, this->renderFinishedSemaphore, nullptr);

    // Destroy image view (not managed by swapchain)
    vkDestroyImageView(vulkan.device, this->deviceImage->getImageView(), nullptr);

    // Reset smart pointers
    this->depthImage.reset();
    this->deviceImage.reset();
    this->renderTarget.reset();
    m_inFlightFence.reset();
  }

  void FrameSubmissionUnit::waitForFence() {
    REN_PROFILE_FUNCTION();
    if (m_inFlightFence) {
      m_inFlightFence->awaitCompletion(true);
      // The fence has been waited on and reset, so it no longer represents
      // pending work. Drop it so a later wait (e.g. from the destructor during
      // a swapchain rebuild) doesn't block forever on a fence nothing will
      // signal.
      m_inFlightFence.reset();
    }
  }

  ref<CommandEncoder> FrameSubmissionUnit::beginFrame() {
    // Just call base class begin() to reset resources and start recording
    return begin();
  }

  void FrameSubmissionUnit::submitAndPresent(SubmissionQueue &queue, VkSwapchainKHR swapchain) {
    // Setup synchronization for swapchain presentation
    VkSemaphore waitSems[] = {imageAvailableSemaphore};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore signalSems[] = {renderFinishedSemaphore};

    SubmissionInfo submitInfo{
        .waitSemaphores = waitSems,
        .waitStages = waitStages,
        .signalSemaphores = signalSems,
    };

    // Submit commands and store fence for next frame's wait
    {
      REN_PROFILE_SCOPE("Submit Graphics Queue");
      m_inFlightFence = submitTo(queue, submitInfo);
    }

    // Present to swapchain
    {
      REN_PROFILE_SCOPE("Presentation");

      VkPresentInfoKHR presentInfo{};
      presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
      presentInfo.waitSemaphoreCount = 1;
      presentInfo.pWaitSemaphores = &renderFinishedSemaphore;
      presentInfo.swapchainCount = 1;
      presentInfo.pSwapchains = &swapchain;
      u32 imageIndex = swapchainImageIndex;
      presentInfo.pImageIndices = &imageIndex;

      auto result = vkQueuePresentKHR(queue.getHandle(), &presentInfo);
      // A resize (or going fullscreen) makes the swapchain out of date. That's
      // not an error: the next acquire will report it and rebuild.
      if (result != VK_ERROR_OUT_OF_DATE_KHR && result != VK_SUBOPTIMAL_KHR) {
        VK_CHECK(result);
      }
    }
  }

}  // namespace ren
