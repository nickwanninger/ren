#pragma once

#include <ren/types.h>
#include <ren/renderer/submission/SubmissionUnit.h>
#include <ren/renderer/Image.h>
#include <ren/renderer/RenderTarget.h>

namespace ren {

  class Swapchain;
  class SubmissionQueue;

  // Frame-specific submission unit for swapchain presentation
  // Extends SubmissionUnit with swapchain-specific resources and presentation logic
  class FrameSubmissionUnit : public SubmissionUnit {
   public:
    FrameSubmissionUnit(u32 slotIndex, Swapchain &swapchain, VkImage swapchainImage,
                        VkImageView swapchainImageView);
    ~FrameSubmissionUnit();

    // Wait for this frame slot's previous work to complete
    void waitForFence();

    // Begin a new frame: reset resources and start command recording
    ref<CommandEncoder> beginFrame();

    // Submit to queue and present to swapchain
    // Automatically handles semaphore synchronization for swapchain presentation
    void submitAndPresent(SubmissionQueue &queue, VkSwapchainKHR swapchain);

    // Frame-specific resources
    const u32 slotIndex;
    u32 swapchainImageIndex;
    RenderTargetRef renderTarget = nullptr;
    ImageRef deviceImage = nullptr;
    ImageRef depthImage = nullptr;

    // Swapchain synchronization
    VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;

   private:
    Swapchain &m_swapchain;
    ref<Fence> m_inFlightFence = nullptr;  // Fence from last submission
  };

}  // namespace ren
