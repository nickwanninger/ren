#pragma once

#include <fmt/core.h>
#include <iostream>
#include <string>
#include <ren/types.h>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#include <SDL2/SDL.h>         // for SDL_Window
#include <SDL2/SDL_vulkan.h>  // for SDL_Vulkan functions

#include <vk_mem_alloc.h>  // for VMA (Vulkan Memory Allocator)

#define CHECK_VK_RESULT(x, msg)                                          \
  do {                                                                   \
    VkResult err = x;                                                    \
    if (err != VK_SUCCESS) {                                             \
      std::cerr << "Vulkan error: " << msg << " - " << err << std::endl; \
      abort();                                                           \
    }                                                                    \
  } while (0)

namespace ren {
  // Every vulkan application needs at least one Vulkan instance.
  // This class also contains the physical device, device, and surface.
  class VulkanInstance {
   public:
    VulkanInstance(const std::string &app_name, SDL_Window *window);

    ~VulkanInstance();

    SDL_Window *window = nullptr;

    // This is the Vulkan instance handle. We expose it publically so that other
    // parts of the vulkan engine can interface with it.
    VkInstance instance = VK_NULL_HANDLE;

    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphics_queue = VK_NULL_HANDLE;
    u32 graphics_queue_family = 0;


    // ---- Memory Allocator ---- //
    VmaAllocator allocator;

    // The surface is the window that we render to (we link against SDL2)
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;

    // ---- Swapchain ---- //

    // The swapchain is the collection of images that we render to.
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    // The images in the swapchain.
    std::vector<VkImage> images;
    // The image views for the swapchain images.
    std::vector<VkImageView> image_views;
    // The format of the swapchain images.
    VkFormat image_format = VK_FORMAT_UNDEFINED;
    // The extent of the swapchain images.
    VkExtent2D extent = {};

    // ---- Render Pass ---- //

    VkRenderPass render_pass = VK_NULL_HANDLE;

    // ---- Pipeline ---- //

    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline graphics_pipeline;

    // ---- Framebuffers ---- //

    std::vector<VkFramebuffer> swapchain_framebuffers;

    // ---- Command Pool ---- //

    VkCommandPool commandPool;
    // We should somehow merge this and the semaphores into a single struct per frame/image
    std::vector<VkCommandBuffer> commandBuffers;


    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_buffer_memory;



    // ---- Semaphores ---- //
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    bool framebuffer_resized = false;

    u64 frame_number = 0;

    void draw_frame(void);

    void recreate_swapchain(void) {
      vkDeviceWaitIdle(device);
      cleanup_swapchain();
      init_swapchain();
      init_framebuffers();
      framebuffer_resized = false;
    }


    void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                       VkMemoryPropertyFlags properties, VkBuffer &buffer,
                       VkDeviceMemory &bufferMemory);


    void copy_buffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size, u32 srcOffset = 0,
                     u32 dstOffset = 0);

   private:
    void init_instance(void);
    void init_swapchain(void);
    void init_renderpass(void);
    void init_pipeline(void);
    void init_framebuffers(void);
    void init_command_pool(void);
    void init_command_buffer(void);
    void init_sync_objects(void);

    void create_vertex_buffer(void);

    void cleanup_swapchain(void);

    u32 find_memory_type(u32 typeFilter, VkMemoryPropertyFlags properties);

    // writes the commands we want to execute into a command buffer
    void record_command_buffer(VkCommandBuffer commandBuffer, u32 imageIndex);



    VkShaderModule create_shader_module(const std::vector<u8> &code);
    VkShaderModule load_shader_module(const std::string &filename);
  };




  // Represents a buffer in Vulkan memory.
  class Buffer {
   public:
    Buffer(VulkanInstance &vulkan_instance, VkDeviceSize size, VkBufferUsageFlags usage,
           VkMemoryPropertyFlags properties);

    virtual ~Buffer();

    // Non-copyable, movable
    Buffer(const Buffer &) = delete;
    Buffer &operator=(const Buffer &) = delete;
    Buffer(Buffer &&other) noexcept;
    Buffer &operator=(Buffer &&other) noexcept;


    void *map(void);
    void unmap(void);

    void copyFrom(const Buffer &src, VkDeviceSize size, VkDeviceSize srcOffset = 0,
                  VkDeviceSize dstOffset = 0);
    void copyFromHost(const void *data, VkDeviceSize size, VkDeviceSize offset = 0);


    // Getters
    VkBuffer getHandle() const { return buffer; }
    VkDeviceSize getSize() const { return size; }
    bool isMapped() const { return mapped != nullptr; }


   protected:
    VulkanInstance &vulkan;

    VmaAllocation allocation = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize size = 0;

    VkBufferUsageFlags usage;
    VkMemoryPropertyFlags properties;

    void *mapped = nullptr;
  };


};  // namespace ren
