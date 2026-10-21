#include <ren/renderer/vulkan/Vulkan.h>
#include <ren/renderer/shader/ShaderModule.h>
#include <ren/renderer/submission/SubmissionQueue.h>
#include <ren/core/Instrumentation.h>
#include <ren/misc/DeprecationLogger.h>

#include <atomic>
#include <mutex>
#include <vector>
#include <fmt/core.h>
#include <fstream>
#include <imgui/imgui.h>
#include "vkb/VkBootstrap.h"
#include "vulkan/vulkan_core.h"
#include <fmt/core.h>
#include <stb/stb_image.h>

#include <imgui/imconfig.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imstb_rectpack.h>
#include <imstb_textedit.h>
#include <imstb_truetype.h>
#include <imgui/backends/imgui_impl_sdl3.h>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <ren/renderer/Fence.h>
#include <ren/core/Flag.h>


static ren::Flag<bool> validationLayers("validation", true, "Enable Vulkan validation layers");


namespace {
  const char* getMessageTypeLabel(VkDebugUtilsMessageTypeFlagsEXT type) {
    // Can have multiple bits set, prioritize validation > performance > general
    if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
      return "VALIDATION";
    }
    if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
      return "PERFORMANCE";
    }
    if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) {
      return "GENERAL";
    }
    return "UNKNOWN";
  }


  // The layers can call the debug callback from any thread.
  std::atomic<u64> g_validationErrors{0};
  std::mutex g_lastValidationErrorMutex;
  std::string g_lastValidationError;

  void recordValidationError(const char* id, const char* message) {
    std::lock_guard lock(g_lastValidationErrorMutex);
    g_lastValidationError = fmt::format("{} {}", id, message);
    g_validationErrors++;
  }
}  // namespace


u64 ren::getValidationErrorCount(void) { return g_validationErrors.load(); }


std::string ren::getLastValidationError(void) {
  std::lock_guard lock(g_lastValidationErrorMutex);
  return g_lastValidationError;
}

// Custom debug callback - called by validation layers
static VKAPI_ATTR VkBool32 VKAPI_CALL vulkanDebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                          VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                          const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
  // Skip verbose messages unless specifically debugging
  if (messageSeverity < VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    return VK_FALSE;
  }

  if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
    return VK_FALSE;
  }

  const char* typeLabel = getMessageTypeLabel(messageType);


  switch (messageSeverity) {
#define PRINT(SEVERITY, printer)                                                                                \
  case SEVERITY:                                                                                                \
    printer("[VULKAN {}] {} {}", typeLabel, pCallbackData->pMessageIdName ? pCallbackData->pMessageIdName : "", \
            pCallbackData->pMessage ? pCallbackData->pMessage : "");                                            \
    break;


    PRINT(VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT, ren::dbgln);
    PRINT(VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT, ren::dbgln);
    PRINT(VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT, ren::warnln);
    PRINT(VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT, ren::errln);
    default:
      ren::dbgln("[VULKAN {}] {}", typeLabel, pCallbackData->pMessageIdName ? pCallbackData->pMessageIdName : "");
      break;
#undef PRINT
  }

  // if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
  //   ren::errln("^^^ VALIDATION ERROR - This may cause crashes or incorrect rendering ^^^");
  // }

  if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT && (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)) {
    recordValidationError(pCallbackData->pMessageIdName ? pCallbackData->pMessageIdName : "", pCallbackData->pMessage ? pCallbackData->pMessage : "");
  }

  // Return VK_FALSE to continue execution (VK_TRUE would abort the Vulkan call)
  // Only validation layer should ever return VK_TRUE, and only in specific cases
  return VK_FALSE;
}



static ren::VulkanInstance* g_vulkan_instance = nullptr;

ren::VulkanInstance& ren::getVulkan(void) {
  // REN_DEPRECATION_WARNING();
  if (g_vulkan_instance == nullptr) {
    throw std::runtime_error("Vulkan instance not initialized");
  }
  if (!g_vulkan_instance->hasDevice()) {
    throw std::runtime_error("Vulkan device not selected. Call VulkanInstance::selectDevice() first.");
  }
  return *g_vulkan_instance;
}

ren::ref<ren::VulkanInstance> ren::getVulkanRef(void) {
  if (g_vulkan_instance == nullptr) {
    throw std::runtime_error("Vulkan instance not initialized");
  }
  return g_vulkan_instance->shared_from_this();
}

ren::VulkanInstance::VulkanInstance(void) {
  if (g_vulkan_instance != nullptr) {
    throw std::runtime_error("Vulkan instance already initialized");
  }
  g_vulkan_instance = this;

  init_instance();
}


void ren::VulkanInstance::selectDevice(VkSurfaceKHR surface) {
  REN_PROFILE_FUNCTION();
  if (hasDevice()) {
    throw std::runtime_error("Vulkan device already selected");
  }
  if (surface == VK_NULL_HANDLE) {
    throw std::runtime_error("selectDevice() needs a valid surface");
  }

  init_device(surface);
  select_present_format(surface);
  init_command_pool();
}


void ren::VulkanInstance::init_command_pool(void) {
  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = this->graphicsQueue->family();

  if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
    throw std::runtime_error("failed to create command pool!");
  }
}




void ren::VulkanInstance::select_present_format(VkSurfaceKHR surface) {
  u32 count = 0;
  VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count, nullptr));
  std::vector<VkSurfaceFormatKHR> formats(count);
  VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count, formats.data()));

  // In order of preference. The engine writes linear color and relies on an
  // sRGB swapchain to encode it.
  constexpr VkFormat candidates[] = {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB};
  for (VkFormat candidate : candidates) {
    for (const auto& f : formats) {
      if (f.format == candidate && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        this->presentFormat = f.format;
        this->presentColorSpace = f.colorSpace;
        return;
      }
    }
  }

  throw std::runtime_error("The surface supports no sRGB 8-bit swapchain format");
}


void ren::VulkanInstance::init_instance(void) {
  REN_PROFILE_SCOPE("Vulkan Instance Init");
  vkb::InstanceBuilder builder;


  int required_major = 1;
  int required_minor = 3;

  // Configure validation layers with custom debug callback
  auto inst_ret = builder.set_app_name("Example Vulkan Application")
                      .request_validation_layers(validationLayers.get())
                      .set_debug_callback(vulkanDebugCallback)  // Custom callback for formatted output
                      .set_debug_messenger_severity(VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                                                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)  // Skip verbose by default
                      .add_debug_messenger_type(VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
                      .add_debug_messenger_type(VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
                      .require_api_version(required_major, required_minor)
                      .build();

  if (!inst_ret) {
    std::cerr << "Failed to create Vulkan instance: " << inst_ret.error() << std::endl;
    exit(-1);
  }

  vkb::Instance vkb_inst = inst_ret.value();

  // grab the instance and store it away in the VulkanInstance class
  this->instance = vkb_inst.instance;

  // Store the debug messenger handle for proper cleanup (only present when validation enabled)
  this->debug_messenger = vkb_inst.debug_messenger;
  this->vkbInstance = makeBox<vkb::Instance>(vkb_inst);
}


void ren::VulkanInstance::init_device(VkSurfaceKHR surface) {
  REN_PROFILE_SCOPE("Vulkan Device Init");
  int required_major = 1;
  int required_minor = 3;

  // Prefer a compatible discrete GPU. Other GPU types remain fallback options.
  vkb::PhysicalDeviceSelector selector{*vkbInstance};
  selector.prefer_gpu_device_type(vkb::PreferredDeviceType::discrete);
  selector.allow_any_gpu_device_type(false);
  VkPhysicalDeviceFeatures requiredFeatures = {};
  // requiredFeatures.geometryShader = VK_FALSE;    // Enable geometry shaders
  requiredFeatures.samplerAnisotropy = true;  // Enable anisotropic filtering
  // requiredFeatures.fillModeNonSolid = VK_TRUE;
  requiredFeatures.shaderInt64 = true;

  requiredFeatures.multiDrawIndirect = true;         // Batched vkCmdDrawIndexedIndirect
  requiredFeatures.fragmentStoresAndAtomics = true;  // Storage buffer writes from fragment shaders

  selector.set_required_features(requiredFeatures);

  // SV_DrawIndex (gl_DrawID) in the vertex stage, used by the batched material
  // root to index into the instance/object/material arrays.
  VkPhysicalDeviceVulkan11Features vk11Features{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
      .shaderDrawParameters = true,
  };
  selector.set_required_features_11(vk11Features);

  // Request the specific features you need
  VkPhysicalDeviceVulkan12Features vk12Features{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
      .descriptorIndexing = true,
      .shaderSampledImageArrayNonUniformIndexing = true,
      .shaderStorageImageArrayNonUniformIndexing = true,
      .descriptorBindingSampledImageUpdateAfterBind = true,
      .descriptorBindingUpdateUnusedWhilePending = true,
      .descriptorBindingPartiallyBound = true,
      .descriptorBindingVariableDescriptorCount = true,
      .runtimeDescriptorArray = true,
      // Slang uses natural (scalar) layout for data behind pointers, e.g. a
      // Vertex* into the MegaMeshBuffer.
      .scalarBlockLayout = true,
      .bufferDeviceAddress = true,
  };
  selector.set_required_features_12(vk12Features);


  VkPhysicalDeviceVulkan13Features vk13Features{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .synchronization2 = true,
      .dynamicRendering = true,
  };
  selector.set_required_features_13(vk13Features);

  selector.set_minimum_version(required_major, required_minor);
  selector.set_surface(surface);


  auto physicalDeviceResult = selector.select();

  // get physical device properties for version info
  if (!physicalDeviceResult) {
    ren::errln("Failed to select a physical device: {}", physicalDeviceResult.error().message());
    exit(-1);
  }
  auto physicalDevice = physicalDeviceResult.value();




  ren::println("{}, Vulkan {}.{}.{} ", physicalDevice.name, VK_VERSION_MAJOR(physicalDevice.properties.apiVersion),
               VK_VERSION_MINOR(physicalDevice.properties.apiVersion), VK_VERSION_PATCH(physicalDevice.properties.apiVersion));
  if (physicalDevice.properties.limits.maxPushConstantsSize < 128) {
    throw std::runtime_error("REN requires at least 128 bytes of Vulkan push constants");
  }

  this->physical_device = physicalDevice.physical_device;

  // Optional: maps GPU timestamp queries onto the CPU clock of the profiler.
  const char* calibratedTimestampsFn = nullptr;
  if (physicalDevice.enable_extension_if_present(VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME)) {
    calibratedTimestampsFn = "vkGetCalibratedTimestampsKHR";
  } else if (physicalDevice.enable_extension_if_present(VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME)) {
    calibratedTimestampsFn = "vkGetCalibratedTimestampsEXT";
  }

  vkb::DeviceBuilder deviceBuilder{physicalDevice};
  vkb::Device vkbDevice = deviceBuilder.build().value();
  this->device = vkbDevice.device;

  if (calibratedTimestampsFn != nullptr) {
    this->getCalibratedTimestamps = (PFN_vkGetCalibratedTimestampsKHR)vkGetDeviceProcAddr(this->device, calibratedTimestampsFn);
  }




  // Query queue families manually
  uint32_t queue_family_count;
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, nullptr);

  std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_families.data());

  auto tryToMakeQueue = [&vkbDevice](vkb::QueueType type, ref<SubmissionQueue> def) -> ref<SubmissionQueue> {
    auto queue = vkbDevice.get_queue(type);
    auto index = vkbDevice.get_queue_index(type);
    if (!queue || !index) {
      return def;
    }
    return SubmissionQueue::make(queue.value(), index.value());
  };

  // This must be guaranteed.
  this->graphicsQueue = tryToMakeQueue(vkb::QueueType::graphics, nullptr);
  if (this->graphicsQueue == nullptr) {
    throw std::runtime_error("Selected device has no graphics queue");
  }

  // We present on the graphics queue. vk-bootstrap only makes sure that
  // *some* queue family can present to the surface, so verify the graphics
  // family.
  VkBool32 graphicsCanPresent = VK_FALSE;
  VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, this->graphicsQueue->family(), surface, &graphicsCanPresent));
  if (!graphicsCanPresent) {
    throw std::runtime_error("The graphics queue cannot present to the surface");
  }
  // These other queues must be created as well, but can fall back to graphics
  // if not available. Having multiple queues helps performance by allowing
  // parallelism, but them being unique is not strictly needed.
  this->computeQueue = tryToMakeQueue(vkb::QueueType::compute, this->graphicsQueue);
  this->transferQueue = tryToMakeQueue(vkb::QueueType::transfer, this->graphicsQueue);

  // Now that we have an instance, allocate the vulkan allocator
  VmaVulkanFunctions vulkanFunctions = {};
  vulkanFunctions.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
  vulkanFunctions.vkGetDeviceProcAddr = &vkGetDeviceProcAddr;

  VmaAllocatorCreateInfo allocatorCreateInfo = {};
  allocatorCreateInfo.flags = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT | VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
  allocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_3;
  allocatorCreateInfo.physicalDevice = physicalDevice;
  allocatorCreateInfo.device = device;
  allocatorCreateInfo.instance = instance;
  allocatorCreateInfo.pVulkanFunctions = &vulkanFunctions;

  VkResult res = vmaCreateAllocator(&allocatorCreateInfo, &allocator);

  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(physicalDevice, &props);
}

ren::VulkanInstance::~VulkanInstance() {
  // ImGui_ImplVulkan_Shutdown();

  if (hasDevice()) {
    // Queue wrappers call vkQueueWaitIdle in their destructors and therefore
    // must die before the VkDevice.
    transferQueue.reset();
    computeQueue.reset();
    graphicsQueue.reset();

    // Command Pool
    vkDestroyCommandPool(device, commandPool, nullptr);

    vmaDestroyAllocator(allocator);

    vkDestroyDevice(device, nullptr);
  }

  // Destroy debug messenger if validation layers were enabled
  if (debug_messenger != VK_NULL_HANDLE) {
    auto vkDestroyDebugUtilsMessengerEXT =
        reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (vkDestroyDebugUtilsMessengerEXT) {
      vkDestroyDebugUtilsMessengerEXT(instance, debug_messenger, nullptr);
    }
  }

  // Cleanup code for the Vulkan instance
  vkDestroyInstance(instance, nullptr);

  g_vulkan_instance = nullptr;
}




void ren::VulkanInstance::transitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkFormat format, VkImageLayout oldLayout,
                                                VkImageLayout newLayout, VkImageAspectFlags aspect) {
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
  if (oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = 0;  // Present doesn't need specific access
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

  } else  // Common depth buffer transitions for G-buffer rendering
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
      // Initial transition - preparing for geometry pass
      barrier.srcAccessMask = 0;
      barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

      sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
      destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;

    } else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
      // After geometry pass - preparing for lighting pass
      barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

      sourceStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
      destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

    } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
      // Back to depth attachment for next frame
      barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
      barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

      sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
      destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;

    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
      barrier.srcAccessMask = 0;
      barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

      sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
      destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;  // Texture transition

    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

      sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
      destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
      throw std::invalid_argument(fmt::format("Unsupported layout transition {} -> {}!", (u32)oldLayout, (u32)newLayout));
    }

  vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void ren::VulkanInstance::transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) {
  VkCommandBuffer commandBuffer = beginSingleTimeCommands();


  transitionImageLayout(commandBuffer, image, format, oldLayout, newLayout);
  endSingleTimeCommands(commandBuffer);
}




void ren::VulkanInstance::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
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

  vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

  endSingleTimeCommands(commandBuffer);
}




VkFormat ren::VulkanInstance::findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
  for (VkFormat format : candidates) {
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(this->physical_device, format, &props);

    if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
      return format;
    } else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
      return format;
    }
  }

  throw std::runtime_error("failed to find supported format!");
}


bool ren::hasStencilComponent(VkFormat format) { return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT; }




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
  graphicsQueue->submitOne(commandBuffer)->awaitCompletion();

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
    if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }

  throw std::runtime_error("failed to find suitable memory type!");
}




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




const char* ren::VulkanInstance::stringifyEnum(VkDescriptorType type) {
  switch (type) {
    case VK_DESCRIPTOR_TYPE_SAMPLER:
      return "SAMPLER";
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      return "COMBINED_IMAGE_SAMPLER";
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      return "SAMPLED_IMAGE";
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      return "STORAGE_IMAGE";
    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      return "UNIFORM_TEXEL_BUFFER";
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
      return "STORAGE_TEXEL_BUFFER";
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      return "UNIFORM_BUFFER";
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      return "STORAGE_BUFFER";
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      return "UNIFORM_BUFFER_DYNAMIC";
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
      return "STORAGE_BUFFER_DYNAMIC";
    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
      return "INPUT_ATTACHMENT";
    case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK:
      return "INLINE_UNIFORM_BLOCK";
    case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
      return "ACCELERATION_STRUCTURE_KHR";
    case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV:
      return "ACCELERATION_STRUCTURE_NV";
    case VK_DESCRIPTOR_TYPE_SAMPLE_WEIGHT_IMAGE_QCOM:
      return "SAMPLE_WEIGHT_IMAGE_QCOM";
    case VK_DESCRIPTOR_TYPE_BLOCK_MATCH_IMAGE_QCOM:
      return "BLOCK_MATCH_IMAGE_QCOM";
    case VK_DESCRIPTOR_TYPE_MUTABLE_EXT:
      return "MUTABLE_EXT";
    default:
      return "UNKNOWN";
  }
}
