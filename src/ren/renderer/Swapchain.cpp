#include <ren/renderer/Swapchain.h>
#include <ren/renderer/Vulkan.h>
#include <SDL2/SDL_vulkan.h>
#include <fmt/core.h>
#include <vkb/VkBootstrap.h>

static ren::FrameData *g_frameData = nullptr;

namespace ren {
  FrameData &getFrameData(void) {
    if (!g_frameData) {
      throw std::runtime_error("Frame data not initialized. Call Swapchain::init() first.");
    }
    return *g_frameData;
  }


  Swapchain::Swapchain(SDL_Window *window)
      : window(window) {
    auto &vulkan = ren::getVulkan();

    int width, height;
    SDL_Vulkan_GetDrawableSize(window, &width, &height);

    this->deviceExtent.width = width;
    this->deviceExtent.height = height;

    this->renderExtent.height = target_render_height;
    this->renderExtent.width = target_render_width;


    fmt::print("Creating ren::Swapchain for window size: {}x{}\n", width, height);

    // ---- Allocate the Swapchain for device target rendering ---- //
    vkb::SwapchainBuilder swapchain_builder(vulkan.physical_device, vulkan.device, vulkan.surface);

    vkb::Swapchain vkb_swapchain =
        swapchain_builder.use_default_format_selection()
            .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
            .set_desired_format(
                {VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})  // SRGB format
            .set_desired_extent(deviceExtent.width, deviceExtent.height)       // Window size
            .build()
            .value();


    auto images = vkb_swapchain.get_images().value();
    auto imageViews = vkb_swapchain.get_image_views().value();

    this->swapchain = vkb_swapchain.swapchain;
    auto imageFormat = vkb_swapchain.image_format;
    auto depthFormat = vulkan.findDepthFormat();

    fmt::print("Vulkan swapchain created with {} images, extent: {}x{}. Allocating FrameDatas\n",
               images.size(), deviceExtent.width, deviceExtent.height);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      auto &fd = frames[i];
      // ---- Add the swapchain image to the deviceImage in the framedata ---- //
      VkImageCreateInfo imageCreateInfo = {};  // Just so the ren::Image class can have it.
      imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
      imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
      imageCreateInfo.format = imageFormat;
      imageCreateInfo.extent.width = deviceExtent.width;
      imageCreateInfo.extent.height = deviceExtent.height;
      imageCreateInfo.extent.depth = 1;

      fd.deviceImage = ren::Image::create(fmt::format("device #{}", i), images[i], imageViews[i],
                                          VK_NULL_HANDLE,  // Null allocation is a little strange.
                                          imageCreateInfo);


      // ---- Allocate render targets ---- //
      fmt::println("Allocating depth image {}", i);
      fd.depthImage = ren::ImageBuilder(fmt::format("depth #{}", i))
                          .setWidth(deviceExtent.width)
                          .setHeight(deviceExtent.height)
                          .setFormat(depthFormat)
                          .setUsage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
                          .setViewAspectMask(VK_IMAGE_ASPECT_DEPTH_BIT)
                          .build();

      fmt::println("Allocating render image {}", i);
      fd.renderImage =
          ren::ImageBuilder(fmt::format("render #{}", i))
              .setWidth(renderExtent.width)
              .setHeight(renderExtent.height)
              .setFormat(imageFormat)
              .setUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
              .setViewAspectMask(VK_IMAGE_ASPECT_COLOR_BIT)
              .build();

      // ---- Allocate the semaphores and fence for this frame ---- //
    }
    fmt::println("\n\n\n\n");
  }


  Swapchain::~Swapchain() {
    auto &vulkan = ren::getVulkan();
    // wait for idle.
    vkDeviceWaitIdle(vulkan.device);
    fmt::print("Destroying Swapchain with {} frames\n", frames.size());
    for (auto &frame : frames) {
      frame.renderImage.reset();
      frame.depthImage.reset();
      frame.deviceImage.reset();
      vkDestroySemaphore(vulkan.device, frame.imageAvailableSemaphore, nullptr);
      vkDestroySemaphore(vulkan.device, frame.renderFinishedSemaphore, nullptr);
      vkDestroyFence(vulkan.device, frame.inFlightFence, nullptr);
    }

    vkDestroySwapchainKHR(vulkan.device, swapchain, nullptr);
  }

}  // namespace ren