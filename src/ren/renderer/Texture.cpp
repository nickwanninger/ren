#include <ren/renderer/Texture.h>
#include <ren/renderer/Vulkan.h>

#include <stb/stb_image.h>
#include <imgui_impl_vulkan.h>

ren::Texture::Texture(const std::string &name, u32 width, u32 height, u8 *pixels)
    : name(name) {
  ren::ImageBuilder ib(name);

  ib.setWidth(width).setHeight(height);

  VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;

  ib.setFormat(format);
  ib.setUsage(VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
  ib.setAllocationUsage(VMA_MEMORY_USAGE_GPU_ONLY);


  this->image = ib.build();

  // TODO: move to an init function
  auto &vulkan = ren::getVulkan();

  VkDeviceSize imageSize = getWidth() * getHeight() * 4;
  printf("image size: %u bytes\n", imageSize);
  ren::Buffer stagingBuffer(
      vulkan, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  if (pixels != nullptr) stagingBuffer.copyFromHost(pixels, imageSize);

  vulkan.transitionImageLayout(image->getImage(), VK_FORMAT_R8G8B8A8_SRGB,
                               VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  vulkan.copyBufferToImage(stagingBuffer.getHandle(), image->getImage(),
                           static_cast<uint32_t>(width), static_cast<uint32_t>(height));

  vulkan.transitionImageLayout(image->getImage(), VK_FORMAT_R8G8B8A8_SRGB,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  // Texture Sampler
  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = samplerInfo.minFilter = VK_FILTER_NEAREST;

  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

  samplerInfo.anisotropyEnable = VK_TRUE;
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


  // create the imgui texture ID so we can display it in imgui
  imguiTextureID = ImGui_ImplVulkan_AddTexture(sampler, image->getImageView(),
                                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}




ren::Texture::~Texture(void) {
  auto &vulkan = ren::getVulkan();
  // Remove the imgui texture ID first,
  ImGui_ImplVulkan_RemoveTexture(imguiTextureID);

  // then destroy the sampler.
  vkDestroySampler(vulkan.device, sampler, nullptr);
  // And release our image reference.
  this->image.reset();
}


std::shared_ptr<ren::Texture> ren::Texture::load(const std::string &filename) {
  int texWidth, texHeight, texChannels;
  stbi_uc *pixels = stbi_load(filename.data(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

  auto texture =
      std::make_shared<ren::Texture>(filename, (u32)texWidth, (u32)texHeight, (u8 *)pixels);

  stbi_image_free(pixels);

  return texture;
}
