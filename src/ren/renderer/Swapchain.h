#pragma once

#include <ren/types.h>
#include <ren/renderer/vulkan/Vulkan.h>
#include <ren/renderer/Image.h>
#include <SDL2/SDL.h>
#include <ren/renderer/FrameData.h>

namespace ren {

  class Application;

  // Get the current frame data from anywhere in the engine.
  FrameData &getFrameData(void);
  inline u32 getFrameIndex(void) { return ren::getFrameData().frameIndex; }

  constexpr u32 target_render_width = 320;
  constexpr u32 target_render_height = 240;


  // This class implements a swapchain for rendering multiple frames at once.
  // This engine defaults to triple buffering. We also have a lower resolution
  // render target for game assets, and we blit that to the device resolution
  // surface.
  class Swapchain : public ren::VulkanResource, public ren::RefCounted<Swapchain> {
   public:
    // We have one frame for each frame in flight.
    // In a triple buffering setup, this is 3.
    u32 frameIndex = 0;
    std::vector<std::unique_ptr<ren::FrameData>> frames;

    VkExtent2D renderExtent;
    VkExtent2D deviceExtent;

    VkSwapchainKHR swapchain;
    VkFormat imageFormat;
    VkFormat depthFormat;

    SDL_Window *window;


    Swapchain(SDL_Window *window);
    ~Swapchain();


    // Acquire a frame from the swapchain.
    // If this returns NULL, the swapchain is out of date.
    FrameData *acquireNextFrame(void);
  };
}  // namespace ren
