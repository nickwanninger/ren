#pragma once

#include <ren/types.h>
#include <ren/renderer/Vulkan.h>

namespace ren {


  class Sampler {
   public:
    Sampler(VkFilter filter = VK_FILTER_NEAREST);
    ~Sampler(void);

    VkSampler getHandle(void) const { return sampler; }

   private:
    VkSampler sampler = VK_NULL_HANDLE;
  };
}  // namespace ren