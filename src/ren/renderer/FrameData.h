#pragma once

#include <ren/types.h>
#include <ren/renderer/Image.h>
#include <ren/renderer/Texture.h>
#include <ren/renderer/RenderTarget.h>
#include <ren/renderer/Descriptors.h>

namespace ren {


  // TODO: Move me elsewhere!


  class CommandEncoder; // forward declare

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

    // The swapchain render target
    RenderTargetRef renderTarget = nullptr;

    // We then have a device image, which is the final image that is presented
    // to the device in the end. We will blit the render image to this
    // with some fancy up scaling and whatnot.
    ren::ImageRef deviceImage = nullptr;
    ren::ImageRef depthImage = nullptr;  // The depth buffer for rendering.

    // Semaphores for synchronizing the rendering process.

    // Signals when the image is ready to be rendered to.
    VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
    // Signals when the rendering is finished and the image is ready to be presented.
    VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
    // Fence to ensure that the GPU has finished rendering before we can
    // submit the next frame.
    VkFence inFlightFence = VK_NULL_HANDLE;

    // ---- Per frame data, reset at the start of each frame ---- //
    // The command buffer that we record the rendering commands into.
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    ref<CommandEncoder> commandEncoder = nullptr;
    DescriptorAllocator descriptorAllocator;

    // Query pool for GPU performance queries.
    constexpr static u32 query_count = 2;
    VkQueryPool queryPool = VK_NULL_HANDLE;

    FrameData(u32 frameIndex, Swapchain &sc, VkImage swapchainImage,
              VkImageView swapchainImageView);
    ~FrameData();


    // timestamp query
    std::vector<u64> getQueryResults(void);
  };
}  // namespace ren