#include <ren/renderer/Image.h>
#include <ren/renderer/Vulkan.h>

namespace ren {
  static std::unordered_set<Image *> s_images;
  std::unordered_set<Image *> Image::allImages(void) { return s_images; }

  Image::Image(const std::string &name, VkImage image, VkImageView imageView, VmaAllocation memory,
               VkImageCreateInfo &createInfo)
      : name(name)
      , image(image)
      , imageView(imageView)
      , memory(memory)
      , imageCreateInfo(createInfo) {
    assert(image != VK_NULL_HANDLE && imageView != VK_NULL_HANDLE &&
           "Image resources must be valid. Check the Vulkan instance and image creation.");
    s_images.insert(this);
  }

  Image::~Image(void) {
    s_images.erase(this);
    auto &vulkan = ren::getVulkan();

    // If an Image has no memory, it means it is managed elsewhere and we should not actually
    // destroy it here. (e.g., swapchain images)
    if (this->memory == VK_NULL_HANDLE) { return; }


    if (image != VK_NULL_HANDLE) {
      vkDestroyImageView(vulkan.device, imageView, nullptr);

      vmaDestroyImage(vulkan.allocator, image, memory);
    }
  }



  Image::Ref Image::create(const std::string &name, VkImage image, VkImageView imageView,
                           VmaAllocation memory, VkImageCreateInfo &createInfo) {
    return makeRef<Image>(name, image, imageView, memory, createInfo);
  }

  void Image::readPixelToBuffer(VkCommandBuffer cmd, glm::vec2 position, VkBuffer stagingBuffer,
                                VkDeviceSize bufferOffset) {
    // Transition to transfer source
    VkImageMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,  // or whatever your current layout is
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Convert normalized coords to pixel coords
    uint32_t pixelX = (uint32_t)(position.x * getWidth());
    uint32_t pixelY = (uint32_t)(position.y * getHeight());

    VkBufferImageCopy region{
        .bufferOffset = bufferOffset,
        // .bufferRowPitch = 0,
        .bufferImageHeight = 0,
        .imageSubresource =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        .imageOffset = {(int32_t)pixelX, (int32_t)pixelY, 0},
        .imageExtent = {1, 1, 1},
    };
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer, 1,
                           &region);

    // Transition back to original layout
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    std::swap(barrier.oldLayout, barrier.newLayout);
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
  }




  ImageBuilder::ImageBuilder(const std::string &name)
      : name(name) {
    auto format = VK_FORMAT_R8G8B8A8_SRGB;
    // initialize the image create info with sane defaults
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
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
    viewInfo.format = format;
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
