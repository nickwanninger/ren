#pragma once

#include <ren/renderer/vulkan/Vulkan.h>

namespace ren {

  class Fence : public ren::VulkanResource, public RefCounted<Fence> {
   public:
    // No copying
    Fence(const Fence&) = delete;
    Fence& operator=(const Fence&) = delete;
    Fence(bool signaled = false);


    ~Fence();


    void awaitCompletion(bool reset = true);

    void reset(void);


    // TODO: RHI
    VkFence getHandle() const { return fence; }

   private:  // TOOD: RHI
    VkFence fence = VK_NULL_HANDLE;
  };

}  // namespace ren
