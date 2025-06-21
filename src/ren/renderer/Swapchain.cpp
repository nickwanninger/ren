#include <ren/renderer/Swapchain.h>
#include <ren/renderer/Vulkan.h>
#include <SDL2/SDL_vulkan.h>
#include <fmt/core.h>
#include <vkb/VkBootstrap.h>
#include <ren/core/Instrumentation.h>
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
    this->frameIndex = 0;
    auto &vulkan = ren::getVulkan();
    vulkan.frame_number = 0;

    int width, height;
    SDL_Vulkan_GetDrawableSize(window, &width, &height);

    this->deviceExtent.width = width;
    this->deviceExtent.height = height;

    this->renderExtent.width = target_render_width;
    this->renderExtent.height = target_render_height;


    fmt::print("Creating ren::Swapchain for window size: {}x{}\n", width, height);

    // ---- Allocate the Swapchain for device target rendering ---- //
    vkb::SwapchainBuilder swapchain_builder(vulkan.physical_device, vulkan.device, vulkan.surface);

    vkb::Swapchain vkb_swapchain =
        swapchain_builder.use_default_format_selection()
            .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
            .set_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
            .set_desired_format(
                {vulkan.swapchainFormat, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})  // SRGB format
            .set_desired_extent(deviceExtent.width, deviceExtent.height)      // Window size
            .build()
            .value();


    auto images = vkb_swapchain.get_images().value();
    auto imageViews = vkb_swapchain.get_image_views().value();

    this->swapchain = vkb_swapchain.swapchain;
    this->imageFormat = vkb_swapchain.image_format;
    this->depthFormat = vulkan.findDepthFormat();

    fmt::print("Vulkan swapchain created with {} images, extent: {}x{}\n", images.size(),
               deviceExtent.width, deviceExtent.height);

    for (u64 i = 0; i < images.size(); i++) {
      fmt::println("Creating frame data for image {} of swapchain", i);
      frames.push_back(makeBox<ren::FrameData>(i, *this, images[i], imageViews[i]));
    }

    for (u64 i = 0; i < frames.size(); i++) {
      auto &frame = *frames[i];
      fmt::println("Frame {}: frameIndex: {}", i, frame.frameIndex);
    }


    fmt::println("\n\n\n\n");
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


  ren::FrameData *Swapchain::acquireNextFrame(void) {
    REN_PROFILE_FUNCTION();
    auto &vulkan = ren::getVulkan();
    if (frames.empty()) {
      fmt::print("No frames available in swapchain\n");
      return nullptr;
    }
    frameIndex = vulkan.frame_number % frames.size();

    // Get the current frame data.
    auto frameData = frames[frameIndex].get();
    g_frameData = frameData;

    assert(frameData->frameIndex == frameIndex);
    {
      REN_PROFILE_SCOPE("Wait for fences");
      vkWaitForFences(vulkan.device, 1, &frameData->inFlightFence, VK_TRUE, UINT64_MAX);
      vkResetFences(vulkan.device, 1, &frameData->inFlightFence);

      vkResetCommandBuffer(frameData->commandBuffer, 0);
    }



    // fmt::println("Acquiring next image for frame index: {}", frameData->frameIndex);

    auto result = vkAcquireNextImageKHR(vulkan.device, this->swapchain, UINT64_MAX,
                                        frameData->imageAvailableSemaphore, VK_NULL_HANDLE,
                                        &frameData->frameIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
        vulkan.framebuffer_resized) {
      return nullptr;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
      fmt::print("Failed to acquire swapchain image {}\n", (int)result);
      // TODO: what does this mean? I usually see -4 here.
      return nullptr;
    }

    return frameData;
  }

}  // namespace ren