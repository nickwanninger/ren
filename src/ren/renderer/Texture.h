#pragma once

#include <ren/types.h>
#include <ren/renderer/Image.h>
#include <ren/core/Instrumentation.h>
#include <ren/assets/Asset.h>
#include <glm/glm.hpp>

namespace ren {


  // A texture is just a 2D image with a sampler.
  class Texture : public ren::TextureAsset, ren::VulkanResource {
   public:
    // Construct a texture with CPU side pixel data. Expect R8G8B8A8_SRGB format.
    // Use the load methods to create textures.
    Texture(const std::string_view &name, u32 width, u32 height, u8 *data = nullptr);
    explicit Texture(ren::ImageRef image);

    ~Texture();

    // -- //

    // Load a texture from a file path. This will eventually be moved to a resource manager.
    static ref<Texture> load(const std::string_view &filename);
    // load the texture from raw data in memory.
    static ref<Texture> load(const std::string_view &name, void *data, u64 size);

    static inline ref<Texture> create(const std::string_view &name, u32 width, u32 height,
                                      u8 *data = nullptr) {
      return makeRef<Texture>(name, width, height, data);
    }

    static ref<Texture> createSinglePixel(const std::string_view &name, u8 r, u8 g, u8 b, u8 a);

    static const std::vector<Texture *> allTextures(void);
    // -- //

    // Get the name of the texture.
    const std::string &getName(void) const { return name; }
    // Get the width of the texture.
    u32 getWidth(void) const { return this->image->getWidth(); }
    // Get the height of the texture.
    u32 getHeight(void) const { return this->image->getHeight(); }

    // Get the Vulkan image handle.
    ren::Image::Ref getImage(void) const { return this->image; }
    VkImageView getImageView(void) const { return this->image->getImageView(); }

    // Get the Vulkan sampler handle.
    VkSampler getSampler(void) const { return sampler; }

    VkDescriptorSet getImGui(void);

    // ^ren::Asset
    AssetType getType() override { return AssetType::Texture; }

    // Display the texture in the ImGui inspection interface (assume we are in a window)
    void inspect(void);


   private:
    std::string name;

    ren::Image::Ref image;
    VkSampler sampler = VK_NULL_HANDLE;

    VkDescriptorSet imguiTextureID = VK_NULL_HANDLE;
  };

  using TextureRef = ref<Texture>;

}  // namespace ren