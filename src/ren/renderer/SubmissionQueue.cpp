#include "./SubmissionQueue.h"


namespace ren {

  SubmissionQueue::~SubmissionQueue() { waitForIdle(); }


  void SubmissionQueue::waitForIdle() {
    // Wait for the queue to become idle
    vkQueueWaitIdle(queue);
  }


  ref<Fence> SubmissionQueue::submit(std::span<VkCommandBuffer> cmds) {
    // Create a fence to signal when the command buffers have finished executing
    auto fence = ren::make<Fence>(false);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = static_cast<u32>(cmds.size());
    submitInfo.pCommandBuffers = cmds.data();

    // Submit the command buffers to the queue
    if (vkQueueSubmit(queue, 1, &submitInfo, fence->getHandle()) != VK_SUCCESS) {
      throw std::runtime_error("Failed to submit command buffers to queue");
    }

    return fence;
  }

  ref<Fence> SubmissionQueue::submit(VkCommandBuffer cmd) { return submit({&cmd, 1}); }

}  // namespace ren