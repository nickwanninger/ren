#pragma once

#include <ren/types.h>
#include <ren/renderer/Image.h>
#include <ren/renderer/TextureHandle.h>
#include <ren/renderer/SamplerCache.h>
#include <ren/core/Builder.h>
#include <ren/core/Instrumentation.h>
#include <ren/assets/Asset.h>
#include <glm/glm.hpp>

#include <memory>

namespace ren {


  // A texture is just a 2D image with a sampler.
  class Texture : public ren::TextureAsset, ren::VulkanResource {
   public:
    // Construct a texture with CPU side pixel data. Color data (the default)
    // is R8G8B8A8_SRGB. Pass R8G8B8A8_UNORM for data such as normals.
    // Use the load methods to create textures.
    Texture(const std::string_view &name, u32 width, u32 height, u8 *data = nullptr, VkFormat format = VK_FORMAT_R8G8B8A8_UNORM);
    explicit Texture(ren::ImageRef image);
    Texture(ref<Image> image, ref<Sampler> sampler);

    ~Texture();

    // -- //

    // RGBA8 pixels decoded from an encoded image (PNG, JPEG, ...).
    struct DecodedImage {
      struct PixelDeleter {
        void operator()(u8 *pixels) const;
      };
      u32 width = 0;
      u32 height = 0;
      std::unique_ptr<u8, PixelDeleter> pixels;

      explicit operator bool() const { return pixels != nullptr; }
    };

    // Decode an encoded image in memory. This does no Vulkan work, so it is
    // safe to call from worker threads. On failure, the result is empty.
    static DecodedImage decode(const void *data, u64 size);

    // Load a texture from a file path. This will eventually be moved to a resource manager.
    static ref<Texture> load(const std::string_view &filename, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);
    // load the texture from raw data in memory.
    static ref<Texture> load(const std::string_view &name, void *data, u64 size, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);

    static inline ref<Texture> create(const std::string_view &name, u32 width, u32 height,
                                      u8 *data = nullptr, VkFormat format = VK_FORMAT_R8G8B8A8_UNORM) {
      return make<Texture>(name, width, height, data, format);
    }

    static ref<Texture> createSinglePixel(const std::string_view &name, u8 r, u8 g, u8 b, u8 a, VkFormat format = VK_FORMAT_R8G8B8A8_UNORM);

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

    // Get the Vulkan sampler handle (shared, from the sampler cache).
    VkSampler getSampler(void) const { return sampler->getHandle(); }

    // Wire handles for this texture's sampled image and its (shared,
    // deduped) sampler. Transient: fetch at record time, never store
    // across frames.
    SampledImageIndex sampledIndex() const { return image->sampledIndex(); }
    SamplerIndex samplerIndex() const;
    TextureHandle handle() const {
      return TextureHandle::pack(sampledIndex(), samplerIndex());
    }
    const ref<Sampler>& getSamplerRef() const { return sampler; }

    VkDescriptorSet getImGui(void);

    // ^ren::Asset
    AssetType getType() override { return AssetType::Texture; }

    // Display the texture in the ImGui inspection interface (assume we are in a window)
    void inspect(void);


   private:
    std::string name;

    ren::Image::Ref image;
    ref<Sampler> sampler;

    VkDescriptorSet imguiTextureID = VK_NULL_HANDLE;
  };

  using TextureRef = ref<Texture>;




  class TextureBuilder : public ren::ImageBuilder {
   public:
    using ren::ImageBuilder::ImageBuilder;


    ref<Texture> build(void);
  };

}  // namespace ren
