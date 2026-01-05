#pragma once

#include <fmt/core.h>
#include <string>
#include <memory>

#include <ren/types.h>
#include <ren/core/Instrumentation.h>
#include <SDL3/SDL.h>         // for SDL_Window
#include <SDL3/SDL_vulkan.h>  // for SDL_Vulkan functions
#include <ren/misc/DeprecationLogger.h>
#include "./vk_mem_alloc.h"  // for VMA (Vulkan Memory Allocator)

#define CHECK_VK_RESULT(x, msg)                                          \
  do {                                                                   \
    VkResult err = x;                                                    \
    if (err != VK_SUCCESS) {                                             \
      std::cerr << "Vulkan error: " << msg << " - " << err << std::endl; \
      abort();                                                           \
    }                                                                    \
  } while (0)


namespace ren {

  class SubmissionQueue;

  class VulkanInstance;

  VulkanInstance &getVulkan(void);
  ref<VulkanInstance> getVulkanRef(void);


  // Every vulkan application needs at least one Vulkan instance.
  // This class also contains the physical device, device, and surface.
  class VulkanInstance : public RefCounted<VulkanInstance> {
   public:
    VulkanInstance(SDL_Window *window);
    ~VulkanInstance();

    // No copy, no move
    VulkanInstance(const VulkanInstance &) = delete;
    VulkanInstance &operator=(const VulkanInstance &) = delete;
    VulkanInstance(VulkanInstance &&) = delete;
    VulkanInstance &operator=(VulkanInstance &&) = delete;




    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;

    // The surface is the window that we render to (we link against SDL)
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    SDL_Window *window = nullptr;


    // -- Submission Queues -- //
    ref<SubmissionQueue> graphicsQueue;
    ref<SubmissionQueue> computeQueue;
    ref<SubmissionQueue> transferQueue;

    // -- Buffer Memory Allocator -- //
    VmaAllocator allocator;

    // Debug messenger for validation layer output (only used when validation enabled)
    VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;

    // ---- Swapchain ---- //
    VkFormat swapchainFormat;  // chosen in init_instance()

    // ---- Command Pool ---- //
    VkCommandPool commandPool;
    u64 frame_number = 0;






    inline void waitForIdle(void) {
      REN_DEPRECATION_WARNING();
      REN_PROFILE_SCOPE("Wait For Idle");
      vkDeviceWaitIdle(device);
    }


    VkSampler createSampler(VkFilter filter);

    void create_image(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
                      VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage &image,
                      VkDeviceMemory &imageMemory);


    VkImageView create_image_view(VkImage image, VkFormat format,
                                  VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT);


    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);

    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);

    VkShaderModule create_shader_module(const std::vector<u8> &code);
    VkShaderModule load_shader_module(const std::string &filename);

    void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout,
                               VkImageLayout newLayout);
    void transitionImageLayout(VkCommandBuffer buf, VkImage image, VkFormat format,
                               VkImageLayout oldLayout, VkImageLayout newLayout,
                               VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);

    inline auto findDepthFormat(void) {
      return findSupportedFormat(
          {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
          VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    }

    VkFormat findSupportedFormat(const std::vector<VkFormat> &candidates, VkImageTiling tiling,
                                 VkFormatFeatureFlags features);
    static VkSampleCountFlagBits getMaxUsableSampleCount(const VkPhysicalDeviceProperties &props) {
      VkSampleCountFlags counts =
          props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;
      if (counts & VK_SAMPLE_COUNT_8_BIT) return VK_SAMPLE_COUNT_8_BIT;
      if (counts & VK_SAMPLE_COUNT_4_BIT) return VK_SAMPLE_COUNT_4_BIT;
      if (counts & VK_SAMPLE_COUNT_2_BIT) return VK_SAMPLE_COUNT_2_BIT;
      return VK_SAMPLE_COUNT_1_BIT;
    }


    static const char *stringifyEnum(VkDescriptorType type);

   private:
    void init_instance(void);
    void init_command_pool(void);


    void cleanup_swapchain(void);


    u32 find_memory_type(u32 typeFilter, VkMemoryPropertyFlags properties);
  };


  // This is a simple class that holds a reference to the vulkan instance.
  // This ensures that any vulkan resources are destroyed *after* the vulkan
  // instance is destroyed.
  class VulkanResource {
   public:
    VulkanInstance &getVulkan() const { return *vulkanInstance; }

   private:
    ref<VulkanInstance> vulkanInstance = getVulkanRef();
  };

};  // namespace ren
