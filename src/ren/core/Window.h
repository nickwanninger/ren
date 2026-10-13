#pragma once

#include <string>

#include <SDL3/SDL.h>
#include <ren/types.h>
#include <ren/renderer/vulkan/Vulkan.h>
#include <ren/renderer/Swapchain.h>

namespace ren {

  struct WindowDesc {
    std::string title;
    glm::uvec2 size{1280, 720};
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;  // SDL_WINDOW_VULKAN is always added
  };

  // One OS window that we render to. A Window owns its SDL window, its Vulkan
  // surface and its swapchain, and destroys them in reverse order.
  //
  // A Window can exist before VulkanInstance::selectDevice(), because its
  // surface is used to select the device. The swapchain is created later, by
  // the first call to rebuildSwapchain().
  class Window {
   public:
    Window(ref<VulkanInstance> vulkan, const WindowDesc &desc);
    ~Window();

    // No copy, no move
    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;
    Window(Window &&) = delete;
    Window &operator=(Window &&) = delete;

    SDL_Window *getHandle(void) const { return m_window; }
    SDL_WindowID getID(void) const { return SDL_GetWindowID(m_window); }
    VkSurfaceKHR getSurface(void) const { return m_surface; }

    bool hasSwapchain(void) const { return m_swapchain != nullptr; }
    Swapchain &getSwapchain(void) { return *m_swapchain; }

    // Update the resize state from an SDL event. Events for other SDL windows
    // (for example ImGui viewports) are ignored.
    void handleEvent(const SDL_Event &event);

    // True when a resize finished and the swapchain does not match the window.
    bool needsRebuild(void) const { return m_needsRebuild && !m_resizing; }

    // Wait for the device to be idle, then replace the swapchain.
    void rebuildSwapchain(void);

   private:
    ref<VulkanInstance> m_vulkan;
    SDL_Window *m_window = nullptr;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    ref<Swapchain> m_swapchain = nullptr;

    bool m_needsRebuild = false;
    bool m_resizing = false;
  };

}  // namespace ren
