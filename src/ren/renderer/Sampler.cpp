#include <ren/renderer/Sampler.h>


namespace ren {


  Sampler::Sampler(VkFilter filter) {
    REN_PROFILE_FUNCTION();
    auto &vulkan = ren::getVulkan();

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = samplerInfo.minFilter = filter;

    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f;

    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(vulkan.device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
      throw std::runtime_error("failed to create texture sampler!");
    }

    fmt::println("Created sampler: {}", (u64)sampler);
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