#pragma once

#include <ren/types.h>
#include <ren/renderer/vulkan/Vulkan.h>

namespace ren {


  class Sampler : public ren::VulkanResource {
   public:
    Sampler(VkFilter filter = VK_FILTER_NEAREST);
    ~Sampler(void);


    // non copyable, non-movable
    Sampler(const Sampler &) = delete;
    Sampler &operator=(const Sampler &) = delete;
    Sampler(Sampler &&) = delete;
    Sampler &operator=(Sampler &&) = delete;

    VkSampler getHandle(void) const { return sampler; }

   private:
    VkSampler sampler = VK_NULL_HANDLE;
  };
}  // namespace ren