#pragma once

#include <fmt/core.h>
#include <string>
#include <memory>

#include <ren/types.h>
#include <ren/renderer/Buffer.h>
#include <ren/renderer/RenderPass.h>

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

  class VulkanInstance;

  VulkanInstance &getVulkan(void);


  // class FrameData {
  //  public:
  //   FrameData(VulkanInstance &vulkan, u32 frame_index)
  //       : vulkan(vulkan)
  //       , frame_index(frame_index) {
  //     uniform_buffer =
  //         std::make_shared<ren::UniformBuffer<UniformBufferObject>>(vulkan,
  //         "UniformBufferObject");
  //   }

  //   VulkanInstance &vulkan;
  //   u32 frame_index;

  //   // The uniform buffer for this frame
  //   std::shared_ptr<ren::UniformBuffer<UniformBufferObject>> uniform_buffer;

  //   // The command buffer for this frame
  //   VkCommandBuffer command_buffer = VK_NULL_HANDLE;
  // };


  struct UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
  };


  // TEMPORARY VERTEX TYPE
  struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;

    static VkVertexInputBindingDescription get_binding_description() {
      VkVertexInputBindingDescription bindingDescription{};
      bindingDescription.binding = 0;
      bindingDescription.stride = sizeof(Vertex);
      bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

      return bindingDescription;
    }
    static std::array<VkVertexInputAttributeDescription, 3> get_attribute_descriptions() {
      std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};


      // First, we need to describe the vertex input attributes.

      // Position attribute
      attributeDescriptions[0].binding = 0;
      attributeDescriptions[0].location = 0;
      // float: VK_FORMAT_R32_SFLOAT
      // vec2:  VK_FORMAT_R32G32_SFLOAT
      // vec3:  VK_FORMAT_R32G32B32_SFLOAT
      // vec4:  VK_FORMAT_R32G32B32A32_SFLOAT
      attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;  // vec3
      attributeDescriptions[0].offset = offsetof(Vertex, pos);


      // Color
      attributeDescriptions[1].binding = 0;
      attributeDescriptions[1].location = 1;
      attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
      attributeDescriptions[1].offset = offsetof(Vertex, color);


      // Texture
      attributeDescriptions[2].binding = 0;
      attributeDescriptions[2].location = 2;
      attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
      attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

      return attributeDescriptions;
    }
  };




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
    std::shared_ptr<ren::RenderPass> render_pass;


    std::vector<VkFramebuffer> swapchain_framebuffers;
    // ---- Command Pool ---- //
    VkCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;

    // ---- Depth resources ---- //
    VkImage depthImage;
    VkDeviceMemory depthImageMemory;
    VkImageView depthImageView;

    // One for each frame in flight
    std::vector<std::shared_ptr<ren::UniformBuffer<UniformBufferObject>>> uniform_buffers;


    // ---- Semaphores ---- //
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    bool framebuffer_resized = false;

    u32 imageIndex = 0;
    u64 frame_number = 0;

    VkCommandBuffer beginFrame(void);
    void endFrame(void);

    void draw_frame(void);

    void recreate_swapchain(void) {
      vkDeviceWaitIdle(device);
      cleanup_swapchain();
      init_swapchain();
      createDepthResources();
      init_framebuffers();
      framebuffer_resized = false;
      this->imageIndex = 0;
      this->frame_number = 0;
    }


    void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                       VkMemoryPropertyFlags properties, VkBuffer &buffer,
                       VkDeviceMemory &bufferMemory);


    void copy_buffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size, u32 srcOffset = 0,
                     u32 dstOffset = 0);


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

    void update_uniform_buffer(u32 current_image);
    void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout,
                               VkImageLayout newLayout);

    inline auto findDepthFormat(void) {
      return findSupportedFormat(
          {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
          VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    }

   private:
    void init_instance(void);
    void init_swapchain(void);
    void init_renderpass(void);
    void init_framebuffers(void);
    void init_command_pool(void);
    void init_command_buffer(void);
    void init_sync_objects(void);
    void init_imgui(void);


    void createDepthResources(void);
    void createTextureImage(void);
    void createUniformBuffers(void);


    void cleanup_swapchain(void);


    u32 find_memory_type(u32 typeFilter, VkMemoryPropertyFlags properties);

    VkFormat findSupportedFormat(const std::vector<VkFormat> &candidates, VkImageTiling tiling,
                                 VkFormatFeatureFlags features);
  };


};  // namespace ren
