#pragma once

#include <ren/types.h>
#include <ren/renderer/vulkan/Vulkan.h>
#include <ren/renderer/Image.h>
#include <SDL3/SDL.h>
#include <ren/renderer/submission/FrameSubmissionUnit.h>

namespace ren {

  class Application;


  struct SwapchainCreateInfo {
    SDL_Window *window;
    bool enableVSync = true;
  };

  // This class implements a swapchain for rendering multiple frames at once.
  // This engine defaults to triple buffering. We also have a lower resolution
  // render target for game assets, and we blit that to the device resolution
  // surface.
  class Swapchain : public ren::VulkanResource, public ren::RefCounted<Swapchain> {
   public:
    // We have one frame for each frame in flight.
    // In a triple buffering setup, this is 3.
    u32 frameSlotIndex = 0;
    std::vector<std::unique_ptr<ren::FrameSubmissionUnit>> frames;

    VkExtent2D deviceExtent;

    VkSwapchainKHR swapchain;
    VkFormat imageFormat;
    VkFormat depthFormat;

    SDL_Window *window;


    Swapchain(SwapchainCreateInfo &info);
    ~Swapchain();


    // Acquire a frame from the swapchain.
    // If this returns NULL, the swapchain is out of date.
    FrameSubmissionUnit *acquireNextFrame(void);
  };

  // Global accessor for the current frame submission unit
  FrameSubmissionUnit &getFrameUnit(void);

  inline u32 getFrameIndex(void) { return getFrameUnit().slotIndex; }

  template <typename T, typename... Args>
  inline T *frameAlloc(Args &&...args) {
    return getFrameUnit().getArena().push<T>(std::forward<Args>(args)...);
  }
}  // namespace ren
