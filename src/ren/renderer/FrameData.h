#pragma once

#include <ren/types.h>
#include <ren/renderer/Buffer.h>
#include <ren/renderer/Image.h>
#include <ren/renderer/Texture.h>

namespace ren {


  struct UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
  };

  class Swapchain;

  // This is the data that holds all the per-frame data for the swapchain.
  struct FrameData {
    // Which of the frames in flight this is?
    u32 frameIndex;
    // These images are used for rendering the scene. They are at "render
    // resolution", which is typically much lower than the device resolution.
    ren::ImageRef renderImage = nullptr;  // The color image used for rendering.

    // The framebuffer we target when rendering the scene
    VkFramebuffer renderFramebuffer = VK_NULL_HANDLE;


    // We then have a device image, which is the final image that is presented
    // to the device in the end. We will blit the render image to this
    // with some fancy up scaling and whatnot.
    ren::ImageRef deviceImage = nullptr;
    ren::ImageRef depthImage = nullptr;  // The depth buffer for rendering.

    // The framebuffer we target when rendering the device image.
    VkFramebuffer deviceFramebuffer = VK_NULL_HANDLE;


    // Semaphores for synchronizing the rendering process.

    // Signals when the image is ready to be rendered to.
    VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
    // Signals when the rendering is finished and the image is ready to be presented.
    VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
    // Fence to ensure that the GPU has finished rendering before we can
    // submit the next frame.
    VkFence inFlightFence = VK_NULL_HANDLE;

    // The uniform buffer to render this frame's scene.
    ref<UniformBuffer<UniformBufferObject>> uniformBuffer = nullptr;

    // The command buffer that we record the rendering commands into.
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    FrameData(u32 frameIndex, Swapchain &sc, VkImage swapchainImage,
              VkImageView swapchainImageView);
    ~FrameData();
  };
}  // namespace ren