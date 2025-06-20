#pragma once

#include <ren/types.h>
#include <memory>
#include <string>
#include <vulkan/vulkan_core.h>
#include <unordered_set>

namespace ren {

  // This class represents the image resources in the rendering engine.
  // (It is effectively a VkImage and VkImageView wrapper.)
  class Image {
   public:
    using Ref = ref<Image>;
    // Construct an image with the given resources.
    // The resources of these images are owned by this class now.
    // The memory allocation can be NULL, in the event that the image
    // is allocated by vkb::SwapchainBuilder, for example.
    Image(const std::string &name, VkImage image, VkImageView imageView, VmaAllocation memory,
          VkImageCreateInfo &createInfo);
    static Image::Ref create(const std::string &name, VkImage image, VkImageView imageView,
                             VmaAllocation memory, VkImageCreateInfo &createInfo);


    static std::unordered_set<Image *> allImages(void);

    ~Image(void);

    // No move, copy or assignment allowed.
    Image(const Image &) = delete;
    Image &operator=(const Image &) = delete;
    Image(Image &&) = delete;
    Image &operator=(Image &&) = delete;

    // Get the name of the image.
    const std::string &getName(void) const { return name; }
    // Get the Vulkan image handle.
    VkImage getImage(void) const { return image; }
    // Get the Vulkan image view handle.
    VkImageView getImageView(void) const { return imageView; }

    // Get dimensions of the image.
    u32 getWidth(void) const { return imageCreateInfo.extent.width; }
    u32 getHeight(void) const { return imageCreateInfo.extent.height; }
    u32 getDepth(void) const { return imageCreateInfo.extent.depth; }

   private:
    std::string name;
    VkImage image = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VmaAllocation memory = VK_NULL_HANDLE;
    const VkImageCreateInfo imageCreateInfo;
  };


  using ImageRef = ref<Image>;


  class ImageBuilder {
   public:
    ImageBuilder(const std::string &name);

#define IMAGE_BUILDER_SETTER(name, type, member) \
  ImageBuilder &set##name(type newValue) {       \
    this->member = newValue;                     \
    return *this;                                \
  }

    // Image Settings
    IMAGE_BUILDER_SETTER(Type, VkImageType, imageInfo.imageType)
    IMAGE_BUILDER_SETTER(Extent, VkExtent3D, imageInfo.extent)
    IMAGE_BUILDER_SETTER(Width, u32, imageInfo.extent.width)
    IMAGE_BUILDER_SETTER(Height, u32, imageInfo.extent.height)
    IMAGE_BUILDER_SETTER(Depth, u32, imageInfo.extent.depth)

    IMAGE_BUILDER_SETTER(MipLevels, u32, imageInfo.mipLevels)
    IMAGE_BUILDER_SETTER(ArrayLayers, u32, imageInfo.arrayLayers)
    IMAGE_BUILDER_SETTER(Samples, VkSampleCountFlagBits, imageInfo.samples)
    IMAGE_BUILDER_SETTER(Tiling, VkImageTiling, imageInfo.tiling)
    IMAGE_BUILDER_SETTER(Usage, VkImageUsageFlags, imageInfo.usage)
    IMAGE_BUILDER_SETTER(SharingMode, VkSharingMode, imageInfo.sharingMode)
    IMAGE_BUILDER_SETTER(InitialLayout, VkImageLayout, imageInfo.initialLayout)

    // Image View Settings
    IMAGE_BUILDER_SETTER(ViewType, VkImageViewType, viewInfo.viewType)
    IMAGE_BUILDER_SETTER(ViewAspectMask, VkImageAspectFlags, viewInfo.subresourceRange.aspectMask)
    IMAGE_BUILDER_SETTER(ViewBaseMipLevel, u32, viewInfo.subresourceRange.baseMipLevel)
    IMAGE_BUILDER_SETTER(ViewLevelCount, u32, viewInfo.subresourceRange.levelCount)
    IMAGE_BUILDER_SETTER(ViewBaseArrayLayer, u32, viewInfo.subresourceRange.baseArrayLayer)
    IMAGE_BUILDER_SETTER(ViewLayerCount, u32, viewInfo.subresourceRange.layerCount)

    // Allocation settings
    IMAGE_BUILDER_SETTER(AllocationUsage, VmaMemoryUsage, allocCreateInfo.usage)
    IMAGE_BUILDER_SETTER(AllocationFlags, VmaAllocationCreateFlags, allocCreateInfo.flags)

    // Some custom ones
    ImageBuilder &setFormat(VkFormat format) {
      imageInfo.format = format;
      viewInfo.format = format;  // Ensure the view format matches the image format.
      return *this;
    }



#undef IMAGE_BUILDER_SETTER


    Image::Ref build(void);

   private:
    std::string name;
    VkImageCreateInfo imageInfo{};
    VkImageViewCreateInfo viewInfo = {};
    VmaAllocationCreateInfo allocCreateInfo = {};
  };
}  // namespace ren