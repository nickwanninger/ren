#pragma once

#include <vulkan/vulkan.h>
#include <ren/types.h>


namespace ren {

  class DescriptorSet : public RefCounted<DescriptorSet> {
   public:
   private:
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
  };

}  // namespace ren