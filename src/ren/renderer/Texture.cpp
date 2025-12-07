
#include <ren/renderer/Texture.h>
#include <ren/renderer/Buffer.h>
#include <ren/renderer/Vulkan.h>
#include <ren/core/Instrumentation.h>

#include <stb/stb_image.h>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <glm/glm.hpp>
#include <ren/core/Flag.h>


static ren::Flag<bool> enableMipmaps("texture-mipmaps",
                                     "Enable mipmap generation for loaded textures");


static std::vector<ren::Texture *> g_all_textures;


const std::vector<ren::Texture *> ren::Texture::allTextures(void) { return g_all_textures; }

ren::Texture::Texture(const std::string_view &name, u32 width, u32 height, u8 *pixels)
    : name(name) {
  REN_PROFILE_FUNCTION();
  g_all_textures.push_back(this);
  ren::ImageBuilder ib(this->name);

  ib.setWidth(width).setHeight(height);

  VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;

  u32 mipLevels = ren::Image::calculateMipLevels(width, height);

  if (!enableMipmaps.get()) {
    mipLevels = 1;  // disable mipmaps for now.
  }

  fmt::println("Creating texture '{}' ({}x{}, {} mip levels)", name, width, height, mipLevels);
  ib.setFormat(format);
  ib.setMipLevels(mipLevels);
  ib.setUsage(VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
              VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
  ib.setAllocationUsage(VMA_MEMORY_USAGE_GPU_ONLY);


  this->image = ib.build();

  // TODO: move to an init function
  auto &vulkan = ren::getVulkan();

  VkDeviceSize imageSize = getWidth() * getHeight() * 4;
  ren::Buffer stagingBuffer(
      imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  if (pixels != nullptr) stagingBuffer.copyFromHost(pixels, imageSize);

  vulkan.transitionImageLayout(image->getImage(), format, VK_IMAGE_LAYOUT_UNDEFINED,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  vulkan.copyBufferToImage(stagingBuffer.getHandle(), image->getImage(),
                           static_cast<uint32_t>(width), static_cast<uint32_t>(height));


  // Generate mipmaps
  if (mipLevels > 1) { image->generateMipmaps(); }
  // image->saveDebug("debug/");


  vulkan.transitionImageLayout(image->getImage(), format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  // Texture Sampler
  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = samplerInfo.minFilter = VK_FILTER_NEAREST;
  // samplerInfo.magFilter = samplerInfo.minFilter = VK_FILTER_LINEAR;

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
  samplerInfo.maxLod = static_cast<float>(mipLevels - 1);

  if (vkCreateSampler(vulkan.device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
    throw std::runtime_error("failed to create texture sampler!");
  }
}


VkDescriptorSet ren::Texture::getImGui(void) {
  if (imguiTextureID == VK_NULL_HANDLE) {
    // create the imgui texture ID so we can display it in imgui
    imguiTextureID = ImGui_ImplVulkan_AddTexture(sampler, image->getImageView(),
                                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }
  return imguiTextureID;
}


ren::Texture::Texture(ren::ImageRef image) {
  g_all_textures.push_back(this);
  REN_PROFILE_FUNCTION();
  auto &vulkan = ren::getVulkan();
  this->image = image;
  this->name = image->getName();


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


  fmt::println("Creating texture '{}' from existing image", name);

  if (vkCreateSampler(vulkan.device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
    throw std::runtime_error("failed to create texture sampler!");
  }

  // create the imgui texture ID so we can display it in imgui
  imguiTextureID = ImGui_ImplVulkan_AddTexture(this->getSampler(), this->getImageView(),
                                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}



ren::Texture::~Texture(void) {
  g_all_textures.erase(std::remove(g_all_textures.begin(), g_all_textures.end(), this),
                       g_all_textures.end());
  auto &vulkan = ren::getVulkan();
  // Remove the imgui texture ID first,
  if (imguiTextureID != VK_NULL_HANDLE) { ImGui_ImplVulkan_RemoveTexture(imguiTextureID); }

  // then destroy the sampler.
  vkDestroySampler(vulkan.device, sampler, nullptr);
  // And release our image reference.
  this->image.reset();
}


ren::ref<ren::Texture> ren::Texture::load(const std::string_view &filename) {
  REN_PROFILE_FUNCTION();
  int texWidth, texHeight, texChannels;
  stbi_uc *pixels = nullptr;

  {
    REN_PROFILE_SCOPE("stbi_load");
    pixels = stbi_load(filename.data(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
  }

  auto texture = ren::makeRef<ren::Texture>(filename, (u32)texWidth, (u32)texHeight, (u8 *)pixels);

  stbi_image_free(pixels);

  return texture;
}


ren::ref<ren::Texture> ren::Texture::load(const std::string_view &name, void *data, u64 size) {
  REN_PROFILE_FUNCTION();
  int texWidth, texHeight, texChannels;
  stbi_uc *pixels = nullptr;

  {
    REN_PROFILE_SCOPE("stbi_load_from_memory");
    pixels = stbi_load_from_memory((stbi_uc *)data, (int)size, &texWidth, &texHeight, &texChannels,
                                   STBI_rgb_alpha);
  }

  auto texture = ren::makeRef<ren::Texture>(name, (u32)texWidth, (u32)texHeight, (u8 *)pixels);

  stbi_image_free(pixels);

  if (texture == NULL) {
    fmt::print("Failed to load texture from memory: {}\n", name);
    return nullptr;
  }

  return texture;
}

ren::ref<ren::Texture> ren::Texture::createSinglePixel(const std::string_view &name, u8 r, u8 g,
                                                       u8 b, u8 a) {
  u8 data[4];
  data[0] = r;
  data[1] = g;
  data[2] = b;
  data[3] = a;
  return makeRef<Texture>(name, 1, 1, data);
}
