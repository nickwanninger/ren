#pragma once

#include <ren/types.h>
#include <ren/renderer/Buffer.h>
#include <ren/renderer/Image.h>

namespace ren {


  // A texture is just a 2D image with a sampler.
  class Texture {
   public:
    // Construct a texture with CPU side pixel data. Expect R8G8B8A8_SRGB format.
    // Use the load methods to create textures.
    Texture(const std::string &name, u32 width, u32 height, u8 *data = nullptr);

    ~Texture();

    // -- //

    // Load a texture from a file path. This will eventually be moved to a resource manager.
    static std::shared_ptr<Texture> load(const std::string &filename);

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

    VkDescriptorSet getImGui(void) { return imguiTextureID; }


   private:
    std::string name;

    ren::Image::Ref image;
    VkSampler sampler = VK_NULL_HANDLE;

    VkDescriptorSet imguiTextureID = VK_NULL_HANDLE;
  };

}  // namespace ren