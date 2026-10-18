#include <ren/renderer/Swapchain.h>
#include <ren/renderer/vulkan/Vulkan.h>
#include <fmt/core.h>
#include <vkb/VkBootstrap.h>
#include <ren/core/Instrumentation.h>



namespace ren {

  static VkSemaphore createSemaphore(VkDevice device) {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkSemaphore semaphore = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore));
    return semaphore;
  }


  Swapchain::Swapchain(SwapchainCreateInfo &info)
      : window(info.window) {
    REN_PROFILE_SCOPE("Build Swapchain");
    auto &vulkan = ren::getVulkan();

    int width, height;
    SDL_GetWindowSizeInPixels(window, &width, &height);

    this->deviceExtent.width = width;
    this->deviceExtent.height = height;


    // ---- Allocate the Swapchain for device target rendering ---- //
    vkb::SwapchainBuilder swapchain_builder(vulkan.physical_device, vulkan.device, info.surface);


    auto presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    if (info.enableVSync) {
      presentMode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
    }

    vkb::Swapchain vkb_swapchain =
        swapchain_builder.use_default_format_selection()
            .set_desired_present_mode(presentMode)
            .set_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
            .set_desired_format({vulkan.presentFormat, vulkan.presentColorSpace})  // chosen by selectDevice()
            .set_desired_extent(deviceExtent.width, deviceExtent.height)           // Window size
            .set_desired_min_image_count(3)                                        // Triple buffering
            .build()
            .value();


    auto images = vkb_swapchain.get_images().value();
    auto imageViews = vkb_swapchain.get_image_views().value();

    // The driver may clamp our desired extent (surface capabilities can be stale
    // mid-resize, e.g. entering fullscreen), so the images we actually got are
    // the only source of truth for everything sized off the swapchain.
    this->deviceExtent = vkb_swapchain.extent;

    this->swapchain = vkb_swapchain.swapchain;
    this->imageFormat = vkb_swapchain.image_format;
    this->depthFormat = vulkan.findDepthFormat();

    VkImageCreateInfo imageCreateInfo = {};
    imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = imageFormat;
    imageCreateInfo.extent.width = deviceExtent.width;
    imageCreateInfo.extent.height = deviceExtent.height;
    imageCreateInfo.extent.depth = 1;

    m_images.resize(images.size());
    for (size_t i = 0; i < images.size(); i++) {
      auto &slot = m_images[i];

      // Wrap the swapchain image. The swapchain owns the VkImage; we own the view.
      slot.image = ren::Image::create(fmt::format("device #{}", i), images[i], imageViews[i], VK_NULL_HANDLE, imageCreateInfo);

      slot.depthImage = ren::ImageBuilder(fmt::format("depth #{}", i))
                            .setWidth(deviceExtent.width)
                            .setHeight(deviceExtent.height)
                            .setFormat(depthFormat)
                            .setUsage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
                            .setViewAspectMask(VK_IMAGE_ASPECT_DEPTH_BIT)
                            .build();

      RenderTargetDescription renderTargetDesc;
      renderTargetDesc.setupColorAndDepth(slot.image, imageFormat, slot.depthImage, depthFormat);
      slot.renderTarget = ren::make<RenderTarget>(renderTargetDesc);

      slot.renderDone = createSemaphore(vulkan.device);
    }

    for (auto &semaphore : m_imageAvailable) {
      semaphore = createSemaphore(vulkan.device);
    }
  }


  Swapchain::~Swapchain() {
    auto &vulkan = ren::getVulkan();
    // Present gives no fence, so this is the only portable way to know that
    // present no longer holds the semaphores and images.
    vkDeviceWaitIdle(vulkan.device);

    for (auto semaphore : m_imageAvailable) {
      vkDestroySemaphore(vulkan.device, semaphore, nullptr);
    }
    for (auto &slot : m_images) {
      vkDestroySemaphore(vulkan.device, slot.renderDone, nullptr);
      // The framebuffer uses the view, so release it first.
      slot.renderTarget.reset();
      // The view is ours, not the swapchain's.
      vkDestroyImageView(vulkan.device, slot.image->getImageView(), nullptr);
    }
    m_images.clear();
    vkDestroySwapchainKHR(vulkan.device, swapchain, nullptr);
  }


  bool Swapchain::acquire(u32 frameSlot) {
    REN_PROFILE_FUNCTION();
    auto &vulkan = ren::getVulkan();
    assert(frameSlot < m_imageAvailable.size());
    m_frameSlot = frameSlot;

    auto result = vkAcquireNextImageKHR(vulkan.device, this->swapchain, UINT64_MAX, m_imageAvailable[frameSlot], VK_NULL_HANDLE, &m_imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
      ren::dbgln("Swapchain image out of date. Rebuilding...");
      return false;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
      // NOTE: VK_SUBOPTIMAL_KHR means the acquire succeeded and the semaphore
      // has a pending signal, so the frame *must* be submitted. Bailing out here
      // would orphan that signal and leave the semaphore in a state we then
      // destroy during a rebuild.
      ren::errln("Failed to acquire swapchain image {}", (int)result);
      return false;
    }

    return true;
  }

}  // namespace ren
