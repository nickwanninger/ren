#pragma once

#include <ren/types.h>
#include <ren/renderer/Buffer.h>


namespace ren {


  // This class represents a texture in the rendering engine.
  class Texture {
   public:
    // Construct a texture with CPU side pixel data. Expect R8G8B8A8_SRGB format.
    // Use the load methods to create textures.
    Texture(const std::string &name, u32 width, u32 height, u8 *data);

    ~Texture();

    // -- //

    // Load a texture from a file path. This will eventually be moved to a resource manager.
    static std::shared_ptr<Texture> load(const std::string &filename);

    // -- //

    // Get the name of the texture.
    const std::string &getName(void) const { return name; }
    // Get the width of the texture.
    u32 getWidth(void) const { return width; }
    // Get the height of the texture.
    u32 getHeight(void) const { return height; }
    // Get the Vulkan image handle.
    VkImage getImage(void) const { return image; }
    // Get the Vulkan image view handle.
    VkImageView getImageView(void) const { return imageView; }
    // Get the Vulkan sampler handle.
    VkSampler getSampler(void) const { return sampler; }



   private:
    std::string name;
    u32 width = 0;
    u32 height = 0;
    VkImage image = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
  };

}  // namespace ren