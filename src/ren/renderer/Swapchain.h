#pragma once

#include <array>

#include <ren/types.h>
#include <ren/renderer/vulkan/Vulkan.h>
#include <ren/renderer/Image.h>
#include <ren/renderer/RenderTarget.h>
#include <SDL3/SDL.h>
#include <ren/renderer/submission/FrameContext.h>

namespace ren {

  struct SwapchainCreateInfo {
    SDL_Window *window;
    // The caller owns the surface. It must outlive the swapchain.
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    bool enableVSync = true;
  };

  // The swapchain of one window. It owns the resources that belong to a
  // swapchain image (the render target and the semaphore that present waits
  // on), and one acquire semaphore per frame in flight. The per-frame command
  // state is in FrameContext, which the Renderer owns.
  class Swapchain : public ren::VulkanResource, public ren::RefCounted<Swapchain> {
   public:
    VkExtent2D deviceExtent;

    VkSwapchainKHR swapchain;
    VkFormat imageFormat;
    VkFormat depthFormat;

    SDL_Window *window;


    Swapchain(SwapchainCreateInfo &info);
    ~Swapchain();


    // Acquire the next image. Signals the acquire semaphore of `frameSlot`.
    // The caller must first wait for that frame slot's fence. Returns false if
    // the swapchain is out of date and must be rebuilt.
    bool acquire(u32 frameSlot);

    // The image from the last successful acquire().
    u32 currentImageIndex(void) const { return m_imageIndex; }
    const ImageRef &currentImage(void) const { return m_images[m_imageIndex].image; }
    RenderTarget &currentTarget(void) const { return *m_images[m_imageIndex].renderTarget; }

    // Signaled when the current image can be written. Submit waits on it.
    VkSemaphore currentImageAvailable(void) const { return m_imageAvailable[m_frameSlot]; }
    // Signaled by the submit that renders the current image. Present waits on
    // it. One per image, because present can hold it until the image is
    // acquired again.
    VkSemaphore currentRenderDone(void) const { return m_images[m_imageIndex].renderDone; }

   private:
    struct SwapchainImage {
      ImageRef image = nullptr;
      ImageRef depthImage = nullptr;
      RenderTargetRef renderTarget = nullptr;
      VkSemaphore renderDone = VK_NULL_HANDLE;
    };

    std::vector<SwapchainImage> m_images;
    std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> m_imageAvailable{};
    u32 m_imageIndex = 0;
    u32 m_frameSlot = 0;
  };

}  // namespace ren
