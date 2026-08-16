#pragma once

#include <ren/types.h>
#include <memory>
#include <string>
#include <vulkan/vulkan_core.h>
#include <unordered_set>

#include <ren/core/Builder.h>
#include <ren/renderer/vulkan/Vulkan.h>
#include <ren/renderer/TextureHandle.h>

namespace ren {

  // This class represents the image resources in the rendering engine.
  // (It is effectively a VkImage and VkImageView wrapper.)
  class Image : public ren::VulkanResource, public std::enable_shared_from_this<Image> {
   public:
    using Ref = ref<Image>;
    // Construct an image with the given resources.
    // The resources of these images are owned by this class now.
    // The memory allocation can be NULL, in the event that the image
    // is allocated by vkb::SwapchainBuilder, for example.
    Image(const std::string &name, VkImage image, VkImageView imageView, VmaAllocation memory, VkImageCreateInfo &createInfo);
    static Image::Ref create(const std::string &name, VkImage image, VkImageView imageView, VmaAllocation memory, VkImageCreateInfo &createInfo);


    static std::unordered_set<Image *> allImages(void);

    ~Image(void);

    // No move, copy or assignment allowed.
    Image(const Image &) = delete;
    Image &operator=(const Image &) = delete;
    Image(Image &&) = delete;
    Image &operator=(Image &&) = delete;


    void uploadPixels(u8 *pixels);  // must be width/height compatible in RGBA

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
    auto getFormat() const { return imageCreateInfo.format; }
    u32 getMipLevels(void) const { return imageCreateInfo.mipLevels; }

    // Lazily makes this view resident in the sampled-image heap. Index zero
    // is reserved for the invalid/debug image contract.
    SampledImageIndex sampledIndex();

    inline bool isFramebuffer() const { return memory == VK_NULL_HANDLE; }

    auto &createInfo() const { return imageCreateInfo; }

    // Calculate the number of mip levels for given dimensions
    static u32 calculateMipLevels(u32 width, u32 height);

    // Generate mipmaps for this image. Image must have been created with mipLevels > 1
    // and with VK_IMAGE_USAGE_TRANSFER_SRC_BIT usage flag.
    // If cmd is VK_NULL_HANDLE, creates a single-time command buffer.
    void generateMipmaps(VkCommandBuffer cmd = VK_NULL_HANDLE);

    // Debug function: save each mipmap level as a PNG file.
    // Files are saved to the current directory with names like "image_mip0.png", "image_mip1.png",
    // etc. Only works for RGBA8 formats. Requires TRANSFER_SRC_BIT usage flag.
    void saveDebug(const std::string &outputDir = ".") const;

    void readPixelToBuffer(VkCommandBuffer cmd, glm::vec2 position, VkBuffer stagingBuffer, VkDeviceSize bufferOffset);

   private:
    std::string name;
    VkImage image = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VmaAllocation memory = VK_NULL_HANDLE;
    const VkImageCreateInfo imageCreateInfo;
    std::mutex sampledSlotMutex;
    std::optional<SampledImageIndex> sampledSlot;
  };


  using ImageRef = ref<Image>;


  class ImageBuilder {
   public:
    ImageBuilder(const std::string &name);


    // Image Settings
    BUILDER_SETTER(Type, VkImageType, imageInfo.imageType)
    BUILDER_SETTER(Extent, VkExtent3D, imageInfo.extent)
    BUILDER_SETTER(Width, u32, imageInfo.extent.width)
    BUILDER_SETTER(Height, u32, imageInfo.extent.height)
    BUILDER_SETTER(Depth, u32, imageInfo.extent.depth)
    auto &setSize(u32 size) { return this->setWidth(size).setHeight(size); }

    BUILDER_SETTER(MipLevels, u32, imageInfo.mipLevels)
    BUILDER_SETTER(ArrayLayers, u32, imageInfo.arrayLayers)
    BUILDER_SETTER(Samples, VkSampleCountFlagBits, imageInfo.samples)
    BUILDER_SETTER(Tiling, VkImageTiling, imageInfo.tiling)
    BUILDER_SETTER(Usage, VkImageUsageFlags, imageInfo.usage)
    BUILDER_SETTER(SharingMode, VkSharingMode, imageInfo.sharingMode)
    BUILDER_SETTER(InitialLayout, VkImageLayout, imageInfo.initialLayout)

    // Image View Settings
    BUILDER_SETTER(ViewType, VkImageViewType, viewInfo.viewType)
    BUILDER_SETTER(ViewAspectMask, VkImageAspectFlags, viewInfo.subresourceRange.aspectMask)
    BUILDER_SETTER(ViewBaseMipLevel, u32, viewInfo.subresourceRange.baseMipLevel)
    BUILDER_SETTER(ViewLevelCount, u32, viewInfo.subresourceRange.levelCount)
    BUILDER_SETTER(ViewBaseArrayLayer, u32, viewInfo.subresourceRange.baseArrayLayer)
    BUILDER_SETTER(ViewLayerCount, u32, viewInfo.subresourceRange.layerCount)

    // Allocation settings
    BUILDER_SETTER(AllocationUsage, VmaMemoryUsage, allocCreateInfo.usage)
    BUILDER_SETTER(AllocationFlags, VmaAllocationCreateFlags, allocCreateInfo.flags)

    // Some custom ones
    ImageBuilder &setFormat(VkFormat format) {
      imageInfo.format = format;
      viewInfo.format = format;  // Ensure the view format matches the image format.
      return *this;
    }

    Image::Ref build(void);

   private:
    std::string name;
    VkImageCreateInfo imageInfo{};
    VkImageViewCreateInfo viewInfo = {};
    VmaAllocationCreateInfo allocCreateInfo = {};
  };
}  // namespace ren
