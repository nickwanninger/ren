#include <ren/core/Window.h>
#include <ren/core/Flag.h>
#include <ren/core/Instrumentation.h>
#include <ren/renderer/submission/SubmissionQueue.h>
#include <SDL3/SDL_vulkan.h>
#include <fmt/core.h>

namespace ren {

  static ren::Flag<bool> vsyncFlag("vsync", true, "Enable VSync for the swapchain");


  Window::Window(ref<VulkanInstance> vulkan, const WindowDesc &desc)
      : m_vulkan(std::move(vulkan)) {
    REN_PROFILE_FUNCTION();

    {
      REN_PROFILE_SCOPE("SDL_CreateWindow");
      auto flags = desc.flags | SDL_WINDOW_VULKAN;
      m_window = SDL_CreateWindow(desc.title.c_str(), desc.size.x, desc.size.y, flags);
      if (m_window == nullptr) {
        throw std::runtime_error(fmt::format("SDL_CreateWindow failed: {}", SDL_GetError()));
      }
    }

    if (!SDL_Vulkan_CreateSurface(m_window, m_vulkan->instance, nullptr, &m_surface)) {
      SDL_DestroyWindow(m_window);
      throw std::runtime_error(fmt::format("SDL_Vulkan_CreateSurface failed: {}", SDL_GetError()));
    }

    // The first window comes before selectDevice(), which verifies it.
    if (m_vulkan->hasDevice()) {
      try {
        checkPresentSupport();
      } catch (...) {
        vkDestroySurfaceKHR(m_vulkan->instance, m_surface, nullptr);
        SDL_DestroyWindow(m_window);
        throw;
      }
    }
  }


  Window::~Window() {
    // The swapchain waits for the device to be idle in its destructor.
    m_swapchain.reset();
    vkDestroySurfaceKHR(m_vulkan->instance, m_surface, nullptr);
    SDL_DestroyWindow(m_window);
  }


  void Window::handleEvent(const SDL_Event &event) {
    switch (event.type) {
      case SDL_EVENT_WINDOW_RESIZED:
      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        m_needsRebuild = true;
        m_resizing = true;
        break;

      case SDL_EVENT_WINDOW_EXPOSED:
        m_resizing = false;
        break;

      default:
        break;
    }
  }


  void Window::rebuildSwapchain(void) {
    REN_PROFILE_FUNCTION();
    m_vulkan->waitForIdle();
    m_swapchain.reset();

    auto info = ren::SwapchainCreateInfo{
        .window = m_window,
        .surface = m_surface,
        .enableVSync = vsyncFlag.get(),
    };
    m_swapchain = makeBox<ren::Swapchain>(info);
    m_needsRebuild = false;
  }


  void Window::checkPresentSupport(void) const {
    auto &vk = *m_vulkan;

    VkBool32 canPresent = VK_FALSE;
    VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(vk.physical_device, vk.graphicsQueue->family(), m_surface, &canPresent));
    if (!canPresent) {
      throw std::runtime_error("The graphics queue cannot present to this window's surface");
    }

    u32 count = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physical_device, m_surface, &count, nullptr));
    std::vector<VkSurfaceFormatKHR> formats(count);
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physical_device, m_surface, &count, formats.data()));
    for (const auto &f : formats) {
      if (f.format == vk.presentFormat && f.colorSpace == vk.presentColorSpace) {
        return;
      }
    }
    throw std::runtime_error("This window's surface does not support the shared present format");
  }

}  // namespace ren
