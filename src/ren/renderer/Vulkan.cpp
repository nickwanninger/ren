#include <ren/renderer/Vulkan.h>
#include <ren/renderer/Shader.h>
#include <ren/core/Instrumentation.h>


#include <vector>
#include <fmt/core.h>
#include <fstream>
#include "imgui.h"
#include "vkb/VkBootstrap.h"
#include "vulkan/vulkan_core.h"
#include <SDL2/SDL_vulkan.h>
#include <fmt/core.h>
#include <stb/stb_image.h>

#include <imconfig.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imstb_rectpack.h>
#include <imstb_textedit.h>
#include <imstb_truetype.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_vulkan.h>



#include <tracy/tracy/TracyVulkan.hpp>

// Implement vk_mem_alloc here!
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"


static ren::VulkanInstance *g_vulkan_instance = nullptr;

ren::VulkanInstance &ren::getVulkan(void) {
  if (g_vulkan_instance == nullptr) { throw std::runtime_error("Vulkan instance not initialized"); }
  return *g_vulkan_instance;
}

ren::ref<ren::VulkanInstance> ren::getVulkanRef(void) {
  if (g_vulkan_instance == nullptr) { throw std::runtime_error("Vulkan instance not initialized"); }
  return g_vulkan_instance->shared_from_this();
}

ren::VulkanInstance::VulkanInstance(SDL_Window *window) {
  this->window = window;
  if (g_vulkan_instance != nullptr) {
    throw std::runtime_error("Vulkan instance already initialized");
  }
  g_vulkan_instance = this;

  int width, height;
  SDL_Vulkan_GetDrawableSize(window, &width, &height);
  this->extent.width = width;
  this->extent.height = height;

  init_instance();



  // ASAP, create a command pool.
  init_command_pool();


  // -- Tracy Vulkan -- //
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandPool = this->commandPool;
  allocInfo.commandBufferCount = 1;
  VkCommandBuffer tracyCommandBuffer;
  vkAllocateCommandBuffers(this->device, &allocInfo, &tracyCommandBuffer);

  TracyVkContext(this->physical_device, this->device, this->graphics_queue, tracyCommandBuffer);
}




VkCommandBuffer ren::VulkanInstance::beginFrame(void) {
  // Not Needed
  return 0;
}


void ren::VulkanInstance::endFrame(void) {
  // Not Needed
}

void ren::VulkanInstance::draw_frame(void) {
  // Not Needed
}

void ren::VulkanInstance::init_instance(void) {
  REN_PROFILE_FUNCTION();
  vkb::InstanceBuilder builder;

  // make the vulkan instance, with basic debug features
  auto inst_ret = builder.set_app_name("Example Vulkan Application")
                      .request_validation_layers(true)
                      .require_api_version(1, 3, 0)
                      .build();

  if (!inst_ret) {
    std::cerr << "Failed to create Vulkan instance: " << inst_ret.error() << std::endl;
    exit(-1);
  }

  vkb::Instance vkb_inst = inst_ret.value();

  fmt::print("Vulkan instance created with version: {}.{}.{}\n",
             VK_VERSION_MAJOR(vkb_inst.instance_version),
             VK_VERSION_MINOR(vkb_inst.instance_version),
             VK_VERSION_PATCH(vkb_inst.instance_version));

  // grab the instance and store it away in the VulkanInstance class
  this->instance = vkb_inst.instance;

  // Create the vulkan surface from SDL
  SDL_Vulkan_CreateSurface(window, instance, &surface);

  // And select the GPU to use (I think we'd need to figure out how to pick the
  // best one if you have multiple GPUs, but I don't so this is fine for now)
  vkb::PhysicalDeviceSelector selector{vkb_inst};
  VkPhysicalDeviceFeatures requiredFeatures = {};
  requiredFeatures.geometryShader = VK_FALSE;    // Enable geometry shaders
  requiredFeatures.samplerAnisotropy = VK_TRUE;  // Enable anisotropic filtering
  requiredFeatures.fillModeNonSolid = VK_TRUE;


  auto physicalDeviceResult = selector.set_minimum_version(1, 2)
                                  .set_required_features(requiredFeatures)
                                  .set_surface(surface)
                                  .select();


  // get physical device properties for version info
  if (!physicalDeviceResult) {
    fmt::print("Failed to select a physical device: {}\n", physicalDeviceResult.error().message());
    exit(-1);
  }
  auto physicalDevice = physicalDeviceResult.value();


  // Print the selected physical device information
  fmt::print("Selected physical device: {}\n", physicalDevice.name);
  fmt::print("Physical device features:\n");
  fmt::print("  Geometry Shader: {}\n",
             physicalDevice.features.geometryShader ? "Enabled" : "Disabled");
  fmt::print("  Anisotropic Filtering: {}\n",
             physicalDevice.features.samplerAnisotropy ? "Enabled" : "Disabled");
  fmt::print("  Fill Mode Non-Solid: {}\n",
             physicalDevice.features.fillModeNonSolid ? "Enabled" : "Disabled");
  fmt::print("Physical device properties:\n");
  fmt::print("  Driver Version: {}\n", physicalDevice.properties.driverVersion);
  fmt::print("  API Version: {}.{}.{}\n", VK_VERSION_MAJOR(physicalDevice.properties.apiVersion),
             VK_VERSION_MINOR(physicalDevice.properties.apiVersion),
             VK_VERSION_PATCH(physicalDevice.properties.apiVersion));

  this->physical_device = physicalDevice.physical_device;

  vkb::DeviceBuilder deviceBuilder{physicalDevice};
  vkb::Device vkbDevice = deviceBuilder.build().value();
  this->device = vkbDevice.device;

  this->graphics_queue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
  this->graphics_queue_family = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
  fmt::print("Created graphics queue with family index: {}\n", this->graphics_queue_family);


  // Now that we have an instance, allocate the vulkan allocator
  VmaVulkanFunctions vulkanFunctions = {};
  vulkanFunctions.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
  vulkanFunctions.vkGetDeviceProcAddr = &vkGetDeviceProcAddr;

  VmaAllocatorCreateInfo allocatorCreateInfo = {};
  allocatorCreateInfo.flags = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
  allocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_2;
  allocatorCreateInfo.physicalDevice = physicalDevice;
  allocatorCreateInfo.device = device;
  allocatorCreateInfo.instance = instance;
  allocatorCreateInfo.pVulkanFunctions = &vulkanFunctions;

  VkResult res = vmaCreateAllocator(&allocatorCreateInfo, &allocator);

  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(physicalDevice, &props);

  printf("Max bound descriptor sets: %u\n", props.limits.maxBoundDescriptorSets);
  printf("Max samplers per set: %u\n", props.limits.maxDescriptorSetSamplers);
  printf("Max UBOs per stage: %u\n", props.limits.maxPerStageDescriptorUniformBuffers);
  printf("Push constants size: %u bytes\n", props.limits.maxPushConstantsSize);


  this->swapchainFormat =
      findSupportedFormat({VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM},
                          VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
}

ren::VulkanInstance::~VulkanInstance() {
  // ImGui_ImplVulkan_Shutdown();

  // Command Pool
  vkDestroyCommandPool(device, commandPool, nullptr);


  vkDestroySurfaceKHR(instance, surface, nullptr);

  vmaDestroyAllocator(allocator);


  vkDestroyDevice(device, nullptr);

  // Cleanup code for the Vulkan instance
  vkDestroyInstance(instance, nullptr);
}


void ren::VulkanInstance::init_swapchain(void) {
  // REN_PROFILE_FUNCTION();
  // this->swapchain.reset();

  // this->swapchain = makeBox<ren::Swapchain>(this->window);
}


void ren::VulkanInstance::init_renderpass(void) {
  // Not Needed
}



void ren::VulkanInstance::init_framebuffers(void) {
  // Not Needed
}


void ren::VulkanInstance::init_command_pool(void) {
  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = this->graphics_queue_family;

  if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
    throw std::runtime_error("failed to create command pool!");
  }
}


void ren::VulkanInstance::init_command_buffer(void) {
  // Not Needed
}


void ren::VulkanInstance::init_sync_objects(void) {
  // Not Needed
}

void ren::VulkanInstance::createUniformBuffers(void) {
  // Not Needed
}

void ren::VulkanInstance::transitionImageLayout(VkCommandBuffer commandBuffer, VkImage image,
                                                VkFormat format, VkImageLayout oldLayout,
                                                VkImageLayout newLayout,
                                                VkImageAspectFlags aspect) {
  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = aspect;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 0;  // TODO:
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;  // TODO:
  barrier.subresourceRange.levelCount = 1;  // TODO

  VkPipelineStageFlags sourceStage;
  VkPipelineStageFlags destinationStage;

  // Set access masks and pipeline stages based on layouts
  if (oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR &&
      newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = 0;  // Present doesn't need specific access
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

  } else  // Common depth buffer transitions for G-buffer rendering
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
      // Initial transition - preparing for geometry pass
      barrier.srcAccessMask = 0;
      barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

      sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
      destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;

    } else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
      // After geometry pass - preparing for lighting pass
      barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

      sourceStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
      destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

    } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
      // Back to depth attachment for next frame
      barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
      barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

      sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
      destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;

    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
               newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
      barrier.srcAccessMask = 0;
      barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

      sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
      destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;  // Texture transition

    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

      sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
      destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
      throw std::invalid_argument(
          fmt::format("Unsupported layout transition {} -> {}!", (u32)oldLayout, (u32)newLayout));
    }

  vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1,
                       &barrier);
}

void ren::VulkanInstance::transitionImageLayout(VkImage image, VkFormat format,
                                                VkImageLayout oldLayout, VkImageLayout newLayout) {
  VkCommandBuffer commandBuffer = beginSingleTimeCommands();


  transitionImageLayout(commandBuffer, image, format, oldLayout, newLayout);
  endSingleTimeCommands(commandBuffer);
}




void ren::VulkanInstance::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width,
                                            uint32_t height) {
  VkCommandBuffer commandBuffer = beginSingleTimeCommands();

  VkBufferImageCopy region{};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;

  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;

  region.imageOffset = {0, 0, 0};
  region.imageExtent = {width, height, 1};

  vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                         &region);

  endSingleTimeCommands(commandBuffer);
}




VkFormat ren::VulkanInstance::findSupportedFormat(const std::vector<VkFormat> &candidates,
                                                  VkImageTiling tiling,
                                                  VkFormatFeatureFlags features) {
  for (VkFormat format : candidates) {
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(this->physical_device, format, &props);

    if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
      return format;
    } else if (tiling == VK_IMAGE_TILING_OPTIMAL &&
               (props.optimalTilingFeatures & features) == features) {
      return format;
    }
  }

  throw std::runtime_error("failed to find supported format!");
}


bool hasStencilComponent(VkFormat format) {
  return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}


void ren::VulkanInstance::createDepthResources() {
  // Not Needed
}

void ren::VulkanInstance::createTextureImage() {
  // Not Needed
}



VkImageView ren::VulkanInstance::create_image_view(VkImage image, VkFormat format,
                                                   VkImageAspectFlags aspectFlags) {
  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = aspectFlags;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;


  VkImageView imageView;
  if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
    throw std::runtime_error("failed to create image view!");
  }

  return imageView;
}


void ren::VulkanInstance::create_image(uint32_t width, uint32_t height, VkFormat format,
                                       VkImageTiling tiling, VkImageUsageFlags usage,
                                       VkMemoryPropertyFlags properties, VkImage &image,
                                       VkDeviceMemory &imageMemory) {
  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = width;
  imageInfo.extent.height = height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = format;
  imageInfo.tiling = tiling;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = usage;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
    throw std::runtime_error("failed to create image!");
  }

  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(device, image, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = find_memory_type(memRequirements.memoryTypeBits, properties);

  // TODO: do this with VMA
  if (vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
    throw std::runtime_error("failed to allocate image memory!");
  }

  vkBindImageMemory(device, image, imageMemory, 0);
}


VkCommandBuffer ren::VulkanInstance::beginSingleTimeCommands() {
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandPool = commandPool;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer commandBuffer;
  vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer(commandBuffer, &beginInfo);

  return commandBuffer;
}

void ren::VulkanInstance::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
  vkEndCommandBuffer(commandBuffer);

  VkCommandBufferAllocateInfo allocInfo{};
  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;

  vkQueueSubmit(graphics_queue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(graphics_queue);

  vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}



void ren::VulkanInstance::update_uniform_buffer(u32 current_frame) {
  // Not Needed
}


void ren::VulkanInstance::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                        VkMemoryPropertyFlags properties, VkBuffer &buffer,
                                        VkDeviceMemory &bufferMemory) {
  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
    throw std::runtime_error("failed to create buffer!");
  }

  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = find_memory_type(memRequirements.memoryTypeBits, properties);

  if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
    throw std::runtime_error("failed to allocate buffer memory!");
  }

  vkBindBufferMemory(device, buffer, bufferMemory, 0);
}


void ren::VulkanInstance::copy_buffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size,
                                      u32 srcOffset, u32 dstOffset) {
  VkCommandBuffer commandBuffer = beginSingleTimeCommands();

  VkBufferCopy copyRegion{};
  copyRegion.size = size;
  vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

  endSingleTimeCommands(commandBuffer);
}

u32 ren::VulkanInstance::find_memory_type(u32 typeFilter, VkMemoryPropertyFlags properties) {
  // First we need to query info about the available types of memory
  // using vkGetPhysicalDeviceMemoryProperties.
  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(physical_device, &memProperties);


  for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
    if ((typeFilter & (1 << i)) &&
        (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }

  throw std::runtime_error("failed to find suitable memory type!");
}

void ren::VulkanInstance::cleanup_swapchain(void) { abort(); }




VkShaderModule ren::VulkanInstance::create_shader_module(const std::vector<u8> &code) {
  VkShaderModuleCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = code.size();
  createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());

  VkShaderModule shaderModule;
  if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
    throw std::runtime_error("failed to create shader module!");
  }

  return shaderModule;
}



VkShaderModule ren::VulkanInstance::load_shader_module(const std::string &filename) {
  std::vector<u8> code;

  // Load the shader code from the file
  std::ifstream file(filename, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    throw std::runtime_error(fmt::format("Failed to open shader file: {}", filename));
  }
  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);
  code.resize(size);
  if (!file.read(reinterpret_cast<char *>(code.data()), size)) {
    throw std::runtime_error(fmt::format("Failed to read shader file: {}", filename));
  }
  file.close();
  fmt::print("Loading shader from {} ({} bytes)\n", filename, size);

  return create_shader_module(code);
}



void ren::VulkanInstance::init_imgui(void) {
  // Not Needed
}



VkSampler ren::VulkanInstance::createSampler(VkFilter filter) {
  VkSampler sampler;  // we leak this for now.

  // Texture Sampler
  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = samplerInfo.minFilter = filter;

  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

  samplerInfo.anisotropyEnable = VK_FALSE;
  samplerInfo.maxAnisotropy = 1.0f;

  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.mipLodBias = 0.0f;
  samplerInfo.minLod = 0.0f;
  samplerInfo.maxLod = 0.0f;

  if (vkCreateSampler(this->device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
    throw std::runtime_error("failed to create texture sampler!");
  }
  return sampler;
}