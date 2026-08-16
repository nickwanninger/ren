#pragma once

#include <ren/types.h>
#include <ren/renderer/vulkan/Vulkan.h>
#include <ren/renderer/TextureHandle.h>

namespace ren {

  // Full sampler state, used both to create a VkSampler and as the dedup
  // key in SamplerCache. Keep this trivially comparable (operator== is
  // memberwise) so it can be hashed/compared as a POD key.
  struct SamplerDesc {
    VkFilter magFilter = VK_FILTER_NEAREST;
    VkFilter minFilter = VK_FILTER_NEAREST;
    VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    VkSamplerAddressMode addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    float maxAnisotropy = 1.0f;
    float minLod = 0.0f;
    float maxLod = 0.0f;

    friend bool operator==(const SamplerDesc&, const SamplerDesc&) = default;
  };

  class Sampler : public ren::VulkanResource {
   public:
    ~Sampler(void);


    // non copyable, non-movable
    Sampler(const Sampler &) = delete;
    Sampler &operator=(const Sampler &) = delete;
    Sampler(Sampler &&) = delete;
    Sampler &operator=(Sampler &&) = delete;

    VkSampler getHandle(void) const { return sampler; }
    SamplerIndex index() const { return descriptorIndex; }

   private:
    friend class SamplerCache;
    explicit Sampler(const SamplerDesc& desc);
    VkSampler sampler = VK_NULL_HANDLE;
    SamplerIndex descriptorIndex;
  };
}  // namespace ren
