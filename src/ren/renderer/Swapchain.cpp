#include <ren/renderer/Swapchain.h>
#include <ren/renderer/vulkan/Vulkan.h>
#include <SDL3/SDL_vulkan.h>
#include <fmt/core.h>
#include <vkb/VkBootstrap.h>
#include <ren/core/Instrumentation.h>
#include <ren/core/Application.h>
#include <ren/core/Flag.h>



static ren::FrameSubmissionUnit *g_frameUnit = nullptr;



namespace ren {
  FrameSubmissionUnit &getFrameUnit(void) {
    if (!g_frameUnit) {
      throw std::runtime_error("Frame unit not initialized. Call Swapchain::init() first.");
    }
    return *g_frameUnit;
  }


  Swapchain::Swapchain(SwapchainCreateInfo &info)
      : window(info.window) {
    REN_PROFILE_SCOPE("Build Swapchain");
    this->frameIndex = 0;
    auto &vulkan = ren::getVulkan();
    vulkan.frame_number = 0;

    int width, height;
    SDL_GetWindowSizeInPixels(window, &width, &height);

    this->deviceExtent.width = width;
    this->deviceExtent.height = height;

    this->renderExtent.width = target_render_width;
    this->renderExtent.height = target_render_height;



    // ---- Allocate the Swapchain for device target rendering ---- //
    vkb::SwapchainBuilder swapchain_builder(vulkan.physical_device, vulkan.device, vulkan.surface);

    fmt::print("Creating ren::Swapchain for window size: {}x{}, vsync={}\n", width, height,
               info.enableVSync);


    auto presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    if (info.enableVSync) {
      presentMode = VK_PRESENT_MODE_FIFO_KHR;
    }

    vkb::Swapchain vkb_swapchain =
        swapchain_builder.use_default_format_selection()
            .set_desired_present_mode(presentMode)
            .set_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
            .set_desired_format(
                {vulkan.swapchainFormat, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})  // SRGB format
            .set_desired_extent(deviceExtent.width, deviceExtent.height)      // Window size
            .set_desired_min_image_count(3)                                   // Triple buffering
            .build()
            .value();


    auto images = vkb_swapchain.get_images().value();
    auto imageViews = vkb_swapchain.get_image_views().value();

    this->swapchain = vkb_swapchain.swapchain;
    this->imageFormat = vkb_swapchain.image_format;
    this->depthFormat = vulkan.findDepthFormat();

    fmt::print("Vulkan swapchain created with {} images, extent: {}x{}. format={}\n", images.size(),
               deviceExtent.width, deviceExtent.height, (u32)imageFormat);

    for (size_t i = 0; i < images.size(); i++) {
      frames.push_back(makeBox<ren::FrameSubmissionUnit>((u32)i, *this, images[i], imageViews[i]));
    }
  }


  Swapchain::~Swapchain() {
    auto &vulkan = ren::getVulkan();
    // wait for idle.
    vkDeviceWaitIdle(vulkan.device);
    fmt::print("Destroying Swapchain with {} frames\n", frames.size());
    // Clear the swapchain data.
    // TODO: make sure nobody is using any of these!
    frames.clear();
    vkDestroySwapchainKHR(vulkan.device, swapchain, nullptr);
  }


  ren::FrameSubmissionUnit *Swapchain::acquireNextFrame(void) {
    REN_PROFILE_FUNCTION();
    auto &vulkan = ren::getVulkan();
    if (frames.empty()) {
      fmt::print("No frames available in swapchain\n");
      return nullptr;
    }
    frameIndex = vulkan.frame_number % frames.size();

    // Get the current frame unit
    auto frameUnit = frames[frameIndex].get();
    g_frameUnit = frameUnit;

    REN_PROFILE_SCOPE("AcquireNextImage");
    auto result = vkAcquireNextImageKHR(vulkan.device, this->swapchain, UINT64_MAX,
                                        frameUnit->imageAvailableSemaphore, VK_NULL_HANDLE,
                                        &frameUnit->frameIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
      ren::dbgln("Swapchain image out of date. Rebuilding...");
      return nullptr;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
      ren::errln("Failed to acquire swapchain image {}", (int)result);
      return nullptr;
    }

    return frameUnit;
  }

}  // namespace ren
