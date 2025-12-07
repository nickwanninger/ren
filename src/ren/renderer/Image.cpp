#include <ren/renderer/Image.h>
#include <ren/renderer/Vulkan.h>
#include <ren/renderer/Buffer.h>
#include <cmath>
#include <stb/stb_image_write.h>
#include <filesystem>

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

  u32 Image::calculateMipLevels(u32 width, u32 height) {
    return static_cast<u32>(std::floor(std::log2(std::max(width, height)))) + 1;
  }

  void Image::generateMipmaps(VkCommandBuffer cmd) {
    // Check that image was created with multiple mip levels
    if (imageCreateInfo.mipLevels <= 1) {
      throw std::invalid_argument("Image must have mipLevels > 1 to generate mipmaps");
    }

    // Check that image has TRANSFER_SRC usage flag
    if (!(imageCreateInfo.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) {
      throw std::invalid_argument(
          "Image must have VK_IMAGE_USAGE_TRANSFER_SRC_BIT usage flag for mipmap generation");
    }

    auto &vulkan = ren::getVulkan();
    bool ownsCmdBuf = false;

    // Create a single-time command buffer if not provided
    if (cmd == VK_NULL_HANDLE) {
      cmd = vulkan.beginSingleTimeCommands();
      ownsCmdBuf = true;
    }

    u32 mipWidth = imageCreateInfo.extent.width;
    u32 mipHeight = imageCreateInfo.extent.height;

    // Generate each mip level from the previous level
    for (u32 i = 1; i < imageCreateInfo.mipLevels; ++i) {
      // Transition source mip (i-1) from TRANSFER_DST_OPTIMAL to TRANSFER_SRC_OPTIMAL
      {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &barrier);
      }

      // Transition destination mip (i) to TRANSFER_DST_OPTIMAL
      {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = i;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &barrier);
      }

      // Calculate size for next mip level
      u32 nextMipWidth = mipWidth > 1 ? mipWidth / 2 : 1;
      u32 nextMipHeight = mipHeight > 1 ? mipHeight / 2 : 1;

      // Blit from source mip to destination mip with downsampling
      VkImageBlit blit{};
      blit.srcOffsets[0] = {0, 0, 0};
      blit.srcOffsets[1] = {static_cast<int32_t>(mipWidth), static_cast<int32_t>(mipHeight), 1};
      blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      blit.srcSubresource.mipLevel = i - 1;
      blit.srcSubresource.baseArrayLayer = 0;
      blit.srcSubresource.layerCount = 1;
      blit.dstOffsets[0] = {0, 0, 0};
      blit.dstOffsets[1] = {static_cast<int32_t>(nextMipWidth), static_cast<int32_t>(nextMipHeight), 1};
      blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      blit.dstSubresource.mipLevel = i;
      blit.dstSubresource.baseArrayLayer = 0;
      blit.dstSubresource.layerCount = 1;


      VkFilter filter = VK_FILTER_LINEAR;
      vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, filter);

      mipWidth = nextMipWidth;
      mipHeight = nextMipHeight;
    }

    // Transition all mip levels to SHADER_READ_ONLY_OPTIMAL at the end
    // Mip level 0 is in TRANSFER_SRC_OPTIMAL, all others are in TRANSFER_DST_OPTIMAL

    // Transition mip level 0 (which was used as source)
    {
      VkImageMemoryBarrier barrier{};
      barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.image = image;
      barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      barrier.subresourceRange.baseMipLevel = 0;
      barrier.subresourceRange.levelCount = 1;
      barrier.subresourceRange.baseArrayLayer = 0;
      barrier.subresourceRange.layerCount = 1;
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                           nullptr, 0, nullptr, 1, &barrier);
    }

    // Transition mip levels 1+ (which are in TRANSFER_DST_OPTIMAL)
    if (imageCreateInfo.mipLevels > 1) {
      VkImageMemoryBarrier barrier{};
      barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.image = image;
      barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      barrier.subresourceRange.baseMipLevel = 1;
      barrier.subresourceRange.levelCount = imageCreateInfo.mipLevels - 1;
      barrier.subresourceRange.baseArrayLayer = 0;
      barrier.subresourceRange.layerCount = 1;
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                           nullptr, 0, nullptr, 1, &barrier);
    }

    // Submit and cleanup if we created the command buffer
    if (ownsCmdBuf) {
      vulkan.endSingleTimeCommands(cmd);
    }
  }

  void Image::saveDebug(const std::string &outputDir) const {
    // Validate format - only support RGBA8
    if (imageCreateInfo.format != VK_FORMAT_R8G8B8A8_SRGB && imageCreateInfo.format != VK_FORMAT_R8G8B8A8_UNORM) {
      throw std::invalid_argument("saveDebug only supports R8G8B8A8_SRGB and R8G8B8A8_UNORM formats");
    }

    // Create output directory if it doesn't exist
    std::filesystem::create_directories(outputDir);

    auto &vulkan = ren::getVulkan();
    VkCommandBuffer cmd = vulkan.beginSingleTimeCommands();

    u32 mipWidth = imageCreateInfo.extent.width;
    u32 mipHeight = imageCreateInfo.extent.height;

    // Save each mip level
    for (u32 mipLevel = 0; mipLevel < imageCreateInfo.mipLevels; ++mipLevel) {
      // Calculate staging buffer size (RGBA = 4 bytes per pixel)
      VkDeviceSize bufferSize = mipWidth * mipHeight * 4;

      // Create staging buffer
      Buffer stagingBuffer(
          bufferSize,
          VK_BUFFER_USAGE_TRANSFER_DST_BIT,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

      // Transition mip level to TRANSFER_SRC_OPTIMAL
      {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = mipLevel;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &barrier);
      }

      // Copy image to buffer
      {
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = mipLevel;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {mipWidth, mipHeight, 1};

        vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer.getHandle(), 1,
                               &region);
      }

      // Transition mip level back to SHADER_READ_ONLY_OPTIMAL
      {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = mipLevel;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &barrier);
      }

      // Submit and wait for copy to complete before reading buffer
      vulkan.endSingleTimeCommands(cmd);

      // Now read the data from the staging buffer
      const u8 *pixels = reinterpret_cast<const u8 *>(stagingBuffer.map());

      // Build filename
      std::string filename =
          (std::filesystem::path(outputDir) / (name + "_mip" + std::to_string(mipLevel) + ".png")).string();

      // Write PNG
      int result = stbi_write_png(filename.c_str(), mipWidth, mipHeight, 4, pixels,
                                  mipWidth * 4);  // stride = width * 4 bytes per pixel

      if (result == 0) {
        throw std::runtime_error("Failed to write PNG: " + filename);
      }

      fmt::println("Saved mipmap level {} to {}", mipLevel, filename);

      // Unmap memory
      stagingBuffer.unmap();

      // Prepare for next iteration (create new command buffer)
      if (mipLevel < imageCreateInfo.mipLevels - 1) {
        cmd = vulkan.beginSingleTimeCommands();
      }

      // Calculate next mip size
      mipWidth = mipWidth > 1 ? mipWidth / 2 : 1;
      mipHeight = mipHeight > 1 ? mipHeight / 2 : 1;
    }
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
    viewInfo.subresourceRange.levelCount = imageInfo.mipLevels;
    if (vkCreateImageView(vulkan.device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
      throw std::runtime_error("failed to create image view!");
    }


    return Image::create(name, image, imageView, memory, imageInfo);
  }

}  // namespace ren
