#pragma once

#include <ren/types.h>
#include <ren/renderer/Image.h>
#include <ren/renderer/Texture.h>
#include <ren/renderer/RenderTarget.h>
#include <ren/renderer/Descriptors.h>
#include <ren/renderer/Fence.h>
#include <ren/core/Arena.h>

namespace ren {


  // TODO: Move me elsewhere!


  class CommandEncoder;  // forward declare

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
    ref<Fence> inFlightFence = Fence::make(true);

    // ---- Per frame data, reset at the start of each frame ---- //
    // The command buffer that we record the rendering commands into.
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    ref<CommandEncoder> commandEncoder = nullptr;
    DescriptorAllocator descriptorAllocator;

    // Query pool for GPU performance queries.
    constexpr static u32 query_count = 2;
    VkQueryPool queryPool = VK_NULL_HANDLE;


    // Arena for per-frame allocations. All data in here is reset and freed at
    // the start of each frame. Nothing long-lived should ever be allocated here.
    ren::Arena arena{4096, true};

    FrameData(u32 frameIndex, Swapchain &sc, VkImage swapchainImage,
              VkImageView swapchainImageView);
    ~FrameData();


    // timestamp query
    std::vector<u64> getQueryResults(void);
  };


  // Get the current frame data from anywhere in the engine.
  // This code is actually implemented in Swapchain.cpp.
  // TODO: move me elsewhere!
  FrameData &getFrameData(void);
  inline u32 getFrameIndex(void) { return ren::getFrameData().frameIndex; }


  template <typename T, typename... Args>
  inline T *frameAlloc(Args &&...args) {
    FrameData &frameData = ren::getFrameData();
    return frameData.arena.push<T>(std::forward<Args>(args)...);
  }


  // This class wraps a pointer allocated from the current frame's arena.
  // It basically does nothing fancy, and is a simple type-enforced wrapper.
  template <typename T>
  class FrameAllocated {
   public:
    FrameAllocated(T *ptr)
        : ptr(ptr) {}

    template <typename... Args>
    FrameAllocated(Args &&...args)
        : ptr(frameAlloc<T>(std::forward<Args>(args)...)) {}


    inline T *operator->() { return ptr; }
    inline T &operator*() { return *ptr; }
    T *get(void) { return ptr; }
  
   private:
    T *ptr;
  };




}  // namespace ren
