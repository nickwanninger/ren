#pragma once

#include <fmt/core.h>
#include <string>
#include <memory>

#include <ren/types.h>
#include <ren/renderer/Buffer.h>
#include <ren/renderer/RenderPass.h>
#include <ren/renderer/Swapchain.h>
#include <ren/renderer/pipelines/DisplayPipeline.h>

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
    // Written externally by ren::Engine when the window is resized.
    bool framebuffer_resized = false;
    VkExtent2D extent;         // Render target size
    VkFormat swapchainFormat;  // chosen in init_instance()
    box<ren::Swapchain> swapchain;

    // ---- Render Pass ---- //
    // We break our rendering into two passes. The first is the 'render pass',
    // which we use to render the 3D scene. The second is the 'display pass',
    // which we use to display the rendered scene on the screen.
    ref<ren::RenderPass> renderPass;
    ref<ren::RenderPass> displayPass;
    ref<ren::DisplayPipeline> displayPipeline;

    // ---- Command Pool ---- //
    VkCommandPool commandPool;
    u64 frame_number = 0;

    VkCommandBuffer beginFrame(void);
    void endFrame(void);

    void draw_frame(void);

    void recreate_swapchain(void) {
      cleanup_swapchain();
      init_swapchain();
      framebuffer_resized = false;
    }


    VkSampler createSampler(VkFilter filter);

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

    VkFormat findSupportedFormat(const std::vector<VkFormat> &candidates, VkImageTiling tiling,
                                 VkFormatFeatureFlags features);

   private:
    void init_instance(void);
    void init_renderpass(void);
    void init_swapchain(void);
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
  };


};  // namespace ren
