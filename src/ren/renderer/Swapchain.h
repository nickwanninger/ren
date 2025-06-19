#pragma once
#include <ren/types.h>
#include <ren/renderer/Image.h>
#include <SDL2/SDL.h>


namespace ren {
  // This is the data that holds all the per-frame data for the swapchain.
  struct FrameData {
    // These images are used for rendering the scene. They are at "render
    // resolution", which is typically much lower than the device resolution.
    ren::ImageRef renderImage = nullptr;  // The color image used for rendering.
    ren::ImageRef depthImage = nullptr;   // The depth buffer for rendering.


    // We then have a device image, which is the final image that is presented
    // to the device in the end. We will blit the render image to this
    // with some fancy up scaling and whatnot.
    ren::ImageRef deviceImage = nullptr;


    // Semaphores for synchronizing the rendering process.

    // Signals when the image is ready to be rendered to.
    VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
    // Signals when the rendering is finished and the image is ready to be presented.
    VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
    // Fence to ensure that the GPU has finished rendering before we can
    // submit the next frame.
    VkFence inFlightFence = VK_NULL_HANDLE;
  };

  // Get the current frame data from anywhere in the engine.
  FrameData &getFrameData(void);

  constexpr u32 target_render_width = 320;
  constexpr u32 target_render_height = 180;


  // This class implements a swapchain for rendering multiple frames at once.
  // This engine defaults to triple buffering. We also have a lower resolution
  // render target for game assets, and we blit that to the device resolution
  // surface.
  class Swapchain {
   public:
    std::array<FrameData, MAX_FRAMES_IN_FLIGHT> frames;

    VkExtent2D renderExtent;
    VkExtent2D deviceExtent;

    VkSwapchainKHR swapchain;

    SDL_Window *window;

    Swapchain(SDL_Window *window);
    ~Swapchain();
  };
}  // namespace ren