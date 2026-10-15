#pragma once

#include <string>

#include <SDL3/SDL.h>
#include <ren/types.h>
#include <ren/renderer/vulkan/Vulkan.h>
#include <ren/renderer/Swapchain.h>

namespace ren {

  class Application;
  class FrameContext;
  class RenderPassEncoder;

  struct WindowDesc {
    std::string title;
    glm::uvec2 size{1280, 720};
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;  // SDL_WINDOW_VULKAN is always added
  };

  // The OS window that the application renders to. Subclass it to set what
  // the window shows (for example, the editor's EditorWindow).
  //
  // The base class owns the SDL window, the Vulkan surface, the swapchain and
  // ImGui. Subclasses do not touch those; they only implement the on*() hooks.
  //
  // The Application drives a Window through these steps:
  //   1. Constructor: plain members only. No SDL, Vulkan or GPU work.
  //   2. initialize(): creates the SDL window and surface. The surface then
  //      selects the device.
  //   3. initializeUI(): creates the ImGui context and backends. Needs the
  //      device and the Renderer.
  //   4. onInitialize(): the subclass creates its GPU state.
  //   Each frame: handleEvent() for each event, beginFrame(), onUpdate(),
  //   the world tick, then render().
  class Window {
   public:
    Window(void) = default;
    virtual ~Window();

    // No copy, no move
    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;
    Window(Window &&) = delete;
    Window &operator=(Window &&) = delete;

    // Create the SDL window and its Vulkan surface.
    void initialize(ref<VulkanInstance> vulkan, const WindowDesc &desc);
    // Create the ImGui context and its SDL3 and Vulkan backends.
    void initializeUI(void);

    SDL_Window *getHandle(void) const { return m_window; }
    SDL_WindowID getID(void) const { return SDL_GetWindowID(m_window); }
    VkSurfaceKHR getSurface(void) const { return m_surface; }
    glm::uvec2 getSize(void) const;
    bool hasFocus(void) const;

    bool hasSwapchain(void) const { return m_swapchain != nullptr; }
    Swapchain &getSwapchain(void) { return *m_swapchain; }

    // Update the resize state, pass the event to ImGui, then call onEvent().
    // Window events for other SDL windows (for example ImGui viewports) do not
    // change the resize state.
    void handleEvent(const SDL_Event &event);

    // True when a resize finished and the swapchain does not match the window.
    bool needsRebuild(void) const { return m_needsRebuild && !m_resizing; }

    // Wait for the device to be idle, then replace the swapchain.
    void rebuildSwapchain(void);

    // Start the frame.
    void beginFrame(void);

    // Record this window's frame: calls onRender(). If onRender() did not call
    // drawImGui(), this ends the ImGui frame so that the next one can start.
    void render(FrameContext &frame);

   protected:
    friend class Application;

    // Called one time, after the device, the Renderer and ImGui exist.
    virtual void onInitialize(void) {}
    // Called for each SDL event, after the base class handled it.
    virtual void onEvent(const SDL_Event &event) {}
    // Called each frame inside the ImGui frame, before the world tick.
    virtual void onUpdate(float dt) {}
    // Record the frame. It must open a pass on the swapchain target and call
    // drawImGui() in it. The default clears the target and draws ImGui.
    virtual void onRender(FrameContext &frame);

    // Draw the ImGui frame into `pass`. Call it one time per frame, last in
    // the pass that targets the swapchain image.
    void drawImGui(RenderPassEncoder &pass);

   private:
    ref<VulkanInstance> m_vulkan;
    SDL_Window *m_window = nullptr;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    ref<Swapchain> m_swapchain = nullptr;

    bool m_needsRebuild = false;
    bool m_resizing = false;

    VkDescriptorPool m_imguiPool = VK_NULL_HANDLE;
    bool m_imguiDrawn = false;
  };

}  // namespace ren
