#include <ren/renderer/vulkan/Vulkan.h>
#include <ren/renderer/ShaderModule.h>
#include <ren/renderer/SubmissionQueue.h>
#include <ren/core/Instrumentation.h>
#include <ren/misc/DeprecationLogger.h>

#include <vector>
#include <fmt/core.h>
#include <fstream>
#include <imgui/imgui.h>
#include "vkb/VkBootstrap.h"
#include "vulkan/vulkan_core.h"
#include <SDL2/SDL_vulkan.h>
#include <fmt/core.h>
#include <stb/stb_image.h>

#include <imgui/imconfig.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imstb_rectpack.h>
#include <imstb_textedit.h>
#include <imstb_truetype.h>
#include <imgui/backends/imgui_impl_sdl2.h>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <ren/renderer/Fence.h>
#include <ren/core/Flag.h>


static ren::Flag<bool> validationLayers("validation", true, "Enable Vulkan validation layers");


// ---- Custom Vulkan Validation Layer Callback ---- //
// This provides formatted, color-coded validation messages with severity filtering.
// Integrates with vkb's debug messenger setup via set_debug_callback().

namespace {
  // ANSI color codes for terminal output
  constexpr const char* COLOR_RESET = "\033[0m";
  constexpr const char* COLOR_RED = "\033[1;31m";      // Errors
  constexpr const char* COLOR_YELLOW = "\033[1;33m";   // Warnings
  constexpr const char* COLOR_BLUE = "\033[1;34m";     // Info
  constexpr const char* COLOR_CYAN = "\033[1;36m";     // Verbose/Debug
  constexpr const char* COLOR_MAGENTA = "\033[1;35m";  // Performance warnings

  const char* getSeverityColor(VkDebugUtilsMessageSeverityFlagBitsEXT severity) {
    switch (severity) {
      case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: return COLOR_RED;
      case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: return COLOR_YELLOW;
      case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT: return COLOR_BLUE;
      case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: return COLOR_CYAN;
      default: return COLOR_RESET;
    }
  }

  const char* getSeverityLabel(VkDebugUtilsMessageSeverityFlagBitsEXT severity) {
    switch (severity) {
      case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: return "ERROR";
      case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: return "WARNING";
      case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT: return "INFO";
      case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: return "VERBOSE";
      default: return "UNKNOWN";
    }
  }

  const char* getMessageTypeLabel(VkDebugUtilsMessageTypeFlagsEXT type) {
    // Can have multiple bits set, prioritize validation > performance > general
    if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) { return "VALIDATION"; }
    if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) { return "PERFORMANCE"; }
    if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) { return "GENERAL"; }
    return "UNKNOWN";
  }
}  // namespace

// Custom debug callback - called by validation layers
static VKAPI_ATTR VkBool32 VKAPI_CALL
vulkanDebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                    VkDebugUtilsMessageTypeFlagsEXT messageType,
                    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
  // Skip verbose messages unless specifically debugging
  if (messageSeverity < VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) { return VK_FALSE; }

  if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) { return VK_FALSE; }

  const char* typeLabel = getMessageTypeLabel(messageType);


  switch (messageSeverity) {
#define PRINT(SEVERITY, printer)                                                \
  case SEVERITY:                                                                \
    printer("[VULKAN {}] {} {}", typeLabel,                                     \
            pCallbackData->pMessageIdName ? pCallbackData->pMessageIdName : "", \
            pCallbackData->pMessage ? pCallbackData->pMessage : "");            \
    break;


    PRINT(VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT, ren::dbgln);
    PRINT(VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT, ren::dbgln);
    PRINT(VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT, ren::warnln);
    PRINT(VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT, ren::errln);
    default:
      ren::dbgln("[VULKAN {}] {}", typeLabel,
                 pCallbackData->pMessageIdName ? pCallbackData->pMessageIdName : "");
      break;
#undef PRINT
  }

  // if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
  //   ren::errln("^^^ VALIDATION ERROR - This may cause crashes or incorrect rendering ^^^");
  // }

  // Return VK_FALSE to continue execution (VK_TRUE would abort the Vulkan call)
  // Only validation layer should ever return VK_TRUE, and only in specific cases
  return VK_FALSE;
}



static ren::VulkanInstance* g_vulkan_instance = nullptr;

ren::VulkanInstance& ren::getVulkan(void) {
  // REN_DEPRECATION_WARNING();
  if (g_vulkan_instance == nullptr) { throw std::runtime_error("Vulkan instance not initialized"); }
  return *g_vulkan_instance;
}

ren::ref<ren::VulkanInstance> ren::getVulkanRef(void) {
  if (g_vulkan_instance == nullptr) { throw std::runtime_error("Vulkan instance not initialized"); }
  return g_vulkan_instance->shared_from_this();
}

ren::VulkanInstance::VulkanInstance(SDL_Window* window) {
  this->window = window;
  if (g_vulkan_instance != nullptr) {
    throw std::runtime_error("Vulkan instance already initialized");
  }
  g_vulkan_instance = this;


  init_instance();

  // ASAP, create a command pool.
  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = this->graphicsQueue->family();

  if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
    throw std::runtime_error("failed to create command pool!");
  }
}




void ren::VulkanInstance::init_instance(void) {
  REN_PROFILE_FUNCTION();
  vkb::InstanceBuilder builder;

  ren::println("Enabling validation layers: {}", validationLayers.get() ? "Yes" : "No");

  // Configure validation layers with custom debug callback
  auto inst_ret =
      builder.set_app_name("Example Vulkan Application")
          .request_validation_layers(validationLayers.get())
          .set_debug_callback(vulkanDebugCallback)  // Custom callback for formatted output
          .set_debug_messenger_severity(
              VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
              VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
              VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)  // Skip verbose by default
          .add_debug_messenger_type(VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
          .add_debug_messenger_type(VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
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

  // Store the debug messenger handle for proper cleanup (only present when validation enabled)
  this->debug_messenger = vkb_inst.debug_messenger;

  // Create the vulkan surface from SDL
  SDL_Vulkan_CreateSurface(window, instance, &surface);

  // And select the GPU to use (I think we'd need to figure out how to pick the
  // best one if you have multiple GPUs, but I don't so this is fine for now)
  vkb::PhysicalDeviceSelector selector{vkb_inst};
  VkPhysicalDeviceFeatures requiredFeatures = {};
  // requiredFeatures.geometryShader = VK_FALSE;    // Enable geometry shaders
  requiredFeatures.samplerAnisotropy = VK_TRUE;  // Enable anisotropic filtering
  // requiredFeatures.fillModeNonSolid = VK_TRUE;

  selector.set_required_features(requiredFeatures);

  // Request the specific features you need
  VkPhysicalDeviceVulkan12Features vk12Features{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
      .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
      .descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,
      .descriptorBindingPartiallyBound = VK_TRUE,
      .runtimeDescriptorArray = VK_TRUE,
      .descriptorIndexing = VK_TRUE,
  };
  // selector.add_required_extension(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
  // selector.set_required_features_12(vk12Features);


  selector.set_minimum_version(1, 2);
  selector.set_surface(surface);


  auto physicalDeviceResult = selector.select();

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




  // Query queue families manually
  uint32_t queue_family_count;
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, nullptr);

  std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count,
                                           queue_families.data());

  // Iterate over all queue families
  for (uint32_t i = 0; i < queue_family_count; i++) {
    VkQueueFamilyProperties& props = queue_families[i];

    fmt::print("Queue Family {}    x{}    ", i, props.queueCount);
#define PRINTCAP(cap, c) fmt::print("{}", props.queueFlags& cap ? c : "-");

    PRINTCAP(VK_QUEUE_GRAPHICS_BIT, "G");
    PRINTCAP(VK_QUEUE_COMPUTE_BIT, "C");
    PRINTCAP(VK_QUEUE_TRANSFER_BIT, "T");
    PRINTCAP(VK_QUEUE_SPARSE_BINDING_BIT, "S");
    PRINTCAP(VK_QUEUE_PROTECTED_BIT, "P");
    PRINTCAP(VK_QUEUE_VIDEO_DECODE_BIT_KHR, "d");
    PRINTCAP(VK_QUEUE_VIDEO_ENCODE_BIT_KHR, "e");
    PRINTCAP(VK_QUEUE_OPTICAL_FLOW_BIT_NV, "o");
#undef PRINTCAP

    fmt::print("\n");
  }



  auto tryToMakeQueue = [&vkbDevice](vkb::QueueType type,
                                     ref<SubmissionQueue> def) -> ref<SubmissionQueue> {
    auto queue = vkbDevice.get_queue(type);
    auto index = vkbDevice.get_queue_index(type);
    if (!queue || !index) { return def; }
    return SubmissionQueue::make(queue.value(), index.value());
  };



  // This must be guaranteed.
  this->graphicsQueue = tryToMakeQueue(vkb::QueueType::graphics, nullptr);
  // These other queues must be created as well, but can fall back to graphics
  // if not available. Having multiple queues helps performance by allowing
  // parallelism, but them being unique is not strictly needed.
  this->computeQueue = tryToMakeQueue(vkb::QueueType::compute, this->graphicsQueue);
  this->transferQueue = tryToMakeQueue(vkb::QueueType::transfer, this->graphicsQueue);

  ren::println("Graphics Queue: {}", this->graphicsQueue->family());
  ren::println("Compute Queue: {}", this->computeQueue->family());
  ren::println("Transfer Queue: {}", this->transferQueue->family());


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

  fmt::print("Max bound descriptor sets: {}\n", props.limits.maxBoundDescriptorSets);
  fmt::print("Max samplers per set: {}\n", props.limits.maxDescriptorSetSamplers);
  fmt::print("Max UBOs per stage: {}\n", props.limits.maxPerStageDescriptorUniformBuffers);
  fmt::print("Push constants size: {} bytes\n", props.limits.maxPushConstantsSize);

  fmt::print("Pipeline Cache UUID: ");
  for (size_t i = 0; i < VK_UUID_SIZE; i++) {
    fmt::print("{:02x} ", props.pipelineCacheUUID[i]);
  }
  fmt::print("\n");


  this->swapchainFormat =
      findSupportedFormat({VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM},
                          VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);


  // Check out the memory properties
  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

  // Print em out nice.
  ren::println("Memory Heaps:");
  for (uint32_t i = 0; i < memProperties.memoryHeapCount; i++) {
    const VkMemoryHeap& heap = memProperties.memoryHeaps[i];
    ren::println("  Heap {}: Size: {} MB, Flags: {}{}", i, heap.size / (1024 * 1024),
                 (heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) ? "DEVICE_LOCAL" : "HOST_VISIBLE",
                 (heap.flags & VK_MEMORY_HEAP_MULTI_INSTANCE_BIT) ? " | MULTI_INSTANCE" : "");
  }

  ren::println("Memory Types:");
  for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
    const VkMemoryType& type = memProperties.memoryTypes[i];

    std::string props;
    if (type.propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) props += "DEVICE_LOCAL ";
    if (type.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) props += "HOST_VISIBLE ";
    if (type.propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) props += "HOST_COHERENT ";
    if (type.propertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) props += "HOST_CACHED ";
    if (type.propertyFlags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) props += "LAZILY_ALLOCATED ";
    ren::println("  Type {}: Heap: {}, Property Flags: {}", i, type.heapIndex, props);
  }
}

ren::VulkanInstance::~VulkanInstance() {
  // ImGui_ImplVulkan_Shutdown();

  // Command Pool
  vkDestroyCommandPool(device, commandPool, nullptr);


  vkDestroySurfaceKHR(instance, surface, nullptr);

  vmaDestroyAllocator(allocator);


  vkDestroyDevice(device, nullptr);

  // Destroy debug messenger if validation layers were enabled
  if (debug_messenger != VK_NULL_HANDLE) {
    auto vkDestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (vkDestroyDebugUtilsMessengerEXT) {
      vkDestroyDebugUtilsMessengerEXT(instance, debug_messenger, nullptr);
    }
  }

  // Cleanup code for the Vulkan instance
  vkDestroyInstance(instance, nullptr);

  g_vulkan_instance = nullptr;
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




VkFormat ren::VulkanInstance::findSupportedFormat(const std::vector<VkFormat>& candidates,
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




VkImageView ren::VulkanInstance::create_image_view(VkImage image, VkFormat format,
                                                   VkImageAspectFlags aspectFlags) {
  REN_DEPRECATION_WARNING();
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
                                       VkMemoryPropertyFlags properties, VkImage& image,
                                       VkDeviceMemory& imageMemory) {
  REN_DEPRECATION_WARNING();
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

  // Submit the buffer to the queue and wait for it to finish with the fence.
  graphicsQueue->submit({&commandBuffer, 1})->awaitCompletion();

  vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}




// void ren::VulkanInstance::copy_buffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size,
//                                       u32 srcOffset, u32 dstOffset) {
//   REN_DEPRECATION_WARNING();
//   ren::println("Copying buffer: size={} srcOffset={} dstOffset={}", size, srcOffset, dstOffset);
//   VkCommandBuffer commandBuffer = beginSingleTimeCommands();

//   VkBufferCopy copyRegion{};
//   copyRegion.size = size;
//   vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

//   endSingleTimeCommands(commandBuffer);
// }

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




VkShaderModule ren::VulkanInstance::create_shader_module(const std::vector<u8>& code) {
  VkShaderModuleCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = code.size();
  createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

  VkShaderModule shaderModule;
  if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
    throw std::runtime_error("failed to create shader module!");
  }

  return shaderModule;
}



VkShaderModule ren::VulkanInstance::load_shader_module(const std::string& filename) {
  std::vector<u8> code;

  // Load the shader code from the file
  std::ifstream file(filename, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    throw std::runtime_error(fmt::format("Failed to open shader file: {}", filename));
  }
  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);
  code.resize(size);
  if (!file.read(reinterpret_cast<char*>(code.data()), size)) {
    throw std::runtime_error(fmt::format("Failed to read shader file: {}", filename));
  }
  file.close();
  fmt::print("Loading shader from {} ({} bytes)\n", filename, size);

  return create_shader_module(code);
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

  samplerInfo.anisotropyEnable = VK_TRUE;
  samplerInfo.maxAnisotropy = 8.0f;

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
