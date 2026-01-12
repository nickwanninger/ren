#include "./SubmissionQueue.h"


namespace ren {

  SubmissionQueue::~SubmissionQueue() { waitForIdle(); }


  void SubmissionQueue::waitForIdle() {
    // Wait for the queue to become idle
    vkQueueWaitIdle(queue);
  }


  ref<Fence> SubmissionQueue::submit(const SubmissionInfo &info) {
    // Create a fence to signal when the command buffers have finished executing
    auto fence = ren::make<Fence>(false);

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = static_cast<u32>(info.cmds.size());
    submitInfo.pCommandBuffers = info.cmds.data();

    submitInfo.waitSemaphoreCount = static_cast<u32>(info.waitSemaphores.size());
    submitInfo.pWaitSemaphores = info.waitSemaphores.data();
    submitInfo.pWaitDstStageMask = info.waitStages.data();

    submitInfo.signalSemaphoreCount = static_cast<u32>(info.signalSemaphores.size());
    submitInfo.pSignalSemaphores = info.signalSemaphores.data();

    {
      REN_PROFILE_SCOPE("vkQueueSubmit");
      // Submit the command buffers to the queue
      if (vkQueueSubmit(queue, 1, &submitInfo, fence->getHandle()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit command buffers to queue");
      }
    }

    return fence;
  }

}  // namespace ren