#include <ren/core/Window.h>
#include <ren/core/Flag.h>
#include <ren/core/Instrumentation.h>
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
  }


  Window::~Window() {
    // The swapchain waits for the device to be idle in its destructor.
    m_swapchain.reset();
    vkDestroySurfaceKHR(m_vulkan->instance, m_surface, nullptr);
    SDL_DestroyWindow(m_window);
  }


  void Window::handleEvent(const SDL_Event &event) {
    // Other SDL windows (for example ImGui viewports) send window events too.
    if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST && event.window.windowID != getID()) {
      return;
    }

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


}  // namespace ren
