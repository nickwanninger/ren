#include <ren/renderer/Sampler.h>


namespace ren {


  Sampler::Sampler(const SamplerDesc& desc) {
    REN_PROFILE_FUNCTION();
    auto &vulkan = ren::getVulkan();

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = desc.magFilter;
    samplerInfo.minFilter = desc.minFilter;

    samplerInfo.addressModeU = desc.addressModeU;
    samplerInfo.addressModeV = desc.addressModeV;
    samplerInfo.addressModeW = desc.addressModeW;

    samplerInfo.anisotropyEnable = desc.maxAnisotropy > 1.0f ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = desc.maxAnisotropy;

    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    samplerInfo.mipmapMode = desc.mipmapMode;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = desc.minLod;
    samplerInfo.maxLod = desc.maxLod;

    if (vkCreateSampler(vulkan.device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
      throw std::runtime_error("failed to create texture sampler!");
    }
  }

  Sampler::~Sampler(void) {
    REN_PROFILE_FUNCTION();
    if (sampler != VK_NULL_HANDLE) {
      auto &vulkan = ren::getVulkan();
      vkDestroySampler(vulkan.device, sampler, nullptr);
      sampler = VK_NULL_HANDLE;
    }
  }
}  // namespace ren
