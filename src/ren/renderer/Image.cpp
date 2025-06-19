#include <ren/renderer/Image.h>
#include <ren/renderer/Vulkan.h>

namespace ren {

  Image::Image(const std::string &name, VkImage image, VkImageView imageView, VmaAllocation memory,
               VkImageCreateInfo &createInfo)
      : name(name)
      , image(image)
      , imageView(imageView)
      , memory(memory)
      , imageCreateInfo(createInfo) {
    assert(image != VK_NULL_HANDLE && imageView != VK_NULL_HANDLE && memory != VK_NULL_HANDLE &&
           "Image resources must be valid. Check the Vulkan instance and image creation.");
  }

  Image::~Image(void) {
    auto &vulkan = ren::getVulkan();
    if (image != VK_NULL_HANDLE) {
      vkDestroyImageView(vulkan.device, imageView, nullptr);
      vmaDestroyImage(vulkan.allocator, image, memory);
    }
  }



  Image::Ref Image::create(const std::string &name, VkImage image, VkImageView imageView,
                           VmaAllocation memory, VkImageCreateInfo &createInfo) {
    return std::make_shared<Image>(name, image, imageView, memory, createInfo);
  }




  ImageBuilder::ImageBuilder(const std::string &name)
      : name(name) {
    // initialize the image create info with sane defaults
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.extent.width = 0;   // Set later
    imageInfo.extent.height = 0;  // Set later
    imageInfo.extent.depth = 1;   // sane default
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;


    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;  // CONFIGURe
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    // same with the allocation create info.
    allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocCreateInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;  // Ensure dedicated memory
  }

  Image::Ref ImageBuilder::build(void) {
    auto &vulkan = ren::getVulkan();
    VkImage image;
    VmaAllocation memory;  // TODO:
    VkImageView imageView;

    VmaAllocationInfo allocInfo = {};
    vmaCreateImage(vulkan.allocator, &imageInfo, &allocCreateInfo, &image, &memory, &allocInfo);


    viewInfo.image = image;
    if (vkCreateImageView(vulkan.device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
      throw std::runtime_error("failed to create image view!");
    }


    return Image::create(name, image, imageView, memory, imageInfo);
  }

}  // namespace ren
