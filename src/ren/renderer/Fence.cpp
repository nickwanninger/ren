#include "./Fence.h"
#include "ren/renderer/Vulkan.h"



namespace ren {


  Fence::Fence(bool signaled) {
    auto &vk = getVulkan();

    VkFenceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    info.flags = signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0;
    vkCreateFence(vk.device, &info, nullptr, &fence);
  }

  Fence::~Fence() {
    auto &vk = getVulkan();
    vkDestroyFence(vk.device, fence, nullptr);
  }


  void Fence::awaitCompletion(bool reset) {
    auto &vk = getVulkan();
    vkWaitForFences(vk.device, 1, &fence, VK_TRUE, UINT64_MAX);
    if (reset) this->reset();
  }

  void Fence::reset(void) {
    auto &vk = getVulkan();
    vkResetFences(vk.device, 1, &fence);
  }
}  // namespace ren
