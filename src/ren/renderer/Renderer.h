#pragma once


#include <ren/types.h>
#include <ren/renderer/Swapchain.h>
#include <ren/renderer/RenderPass.h>
#include <ren/renderer/Image.h>
#include <ren/renderer/Texture.h>
#include <ren/renderer/Vulkan.h>
#include <SDL2/SDL.h>

namespace ren {

  // This class attempts to provide a generic interface for rendering triangles in a 3D Scene.
  // We render to a renderTarget, which is a ren::Texture, and then have a separate pass which
  // blits that renderTarget to the swapchain image.
  class Renderer {
   public:
    // This is the public interface for the renderer

    Renderer(SDL_Window *window);
    ~Renderer(void);

    // Called at the start of a frame. Sync's with the swapchain and acquires the next frame data.
    void beginFrame(void);
    // Called when the scene is done being rendered, and it should be blitted to the swapchain.
    void finalizeScene(void);
    // Called at the end of the frame to submit everything and present the frame.
    void endFrame(void);

    void waitForIdle(void);



    static Renderer &get(void);

    ren::RenderPass &getRenderPass(void) { return *renderPass; }


   private:
    void initSwapchain();

   private:
    SDL_Window *window;
    ref<VulkanInstance> vulkan = nullptr;
    ref<RenderPass> renderPass;
    ref<Swapchain> swapchain = nullptr;
  };
}  // namespace ren