#include <ren/renderer/Texture.h>
#include <ren/renderer/Vulkan.h>

#include <stb/stb_image.h>

ren::Texture::Texture(const std::string &name, u32 width, u32 height, u8 *pixels)
    : name(name)
    , width(width)
    , height(height) {
  // TODO: move to an init function
  auto &vulkan = ren::getVulkan();


  VkDeviceSize imageSize = width * height * 4;

  {
    ren::Buffer stagingBuffer(
        vulkan, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    stagingBuffer.copyFromHost(pixels, imageSize);



    // now we have a staging buffer with the texture data, we can create the
    // actual texture image

    vulkan.create_image(width, height, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
                        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, imageMemory);

    vulkan.transitionImageLayout(image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vulkan.copyBufferToImage(stagingBuffer.getHandle(), image, static_cast<uint32_t>(width),
                             static_cast<uint32_t>(height));

    vulkan.transitionImageLayout(image, VK_FORMAT_R8G8B8A8_SRGB,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }

  // Now, create the texture image view
  imageView = vulkan.create_image_view(image, VK_FORMAT_R8G8B8A8_SRGB);

  // Texture Sampler
  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = samplerInfo.minFilter = VK_FILTER_LINEAR;

  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

  samplerInfo.anisotropyEnable = VK_FALSE;
  samplerInfo.maxAnisotropy = 1.0f;

  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.mipLodBias = 0.0f;
  samplerInfo.minLod = 0.0f;
  samplerInfo.maxLod = 0.0f;

  if (vkCreateSampler(vulkan.device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
    throw std::runtime_error("failed to create texture sampler!");
  }
}




ren::Texture::~Texture(void) {
  auto &vulkan = ren::getVulkan();
  vkDestroySampler(vulkan.device, sampler, nullptr);
  vkDestroyImageView(vulkan.device, imageView, nullptr);
  vkDestroyImage(vulkan.device, image, nullptr);
  vkFreeMemory(vulkan.device, imageMemory, nullptr);
}


std::shared_ptr<ren::Texture> ren::Texture::load(const std::string &filename) {
  int texWidth, texHeight, texChannels;
  stbi_uc *pixels = stbi_load(filename.data(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

  auto texture =
      std::make_shared<ren::Texture>(filename, (u32)texWidth, (u32)texHeight, (u8 *)pixels);

  stbi_image_free(pixels);

  return texture;
}
