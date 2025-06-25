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
    // Called at the end of the frame to submit everything and present the frame.
    void endFrame(void);

    void waitForIdle(void);


    void beginPass(ren::RenderPass &pass, ren::RenderTarget &target);
    void endPass(void);

    template <typename Fn>
    inline void withPass(ren::RenderPass &pass, ren::RenderTarget &target, Fn &&func) {
      REN_PROFILE_FUNCTION();
      beginPass(pass, target);
      func();
      endPass();
    }




    static Renderer &get(void);

    ren::RenderPass &getRenderPass(void) { return *renderPass; }
    ref<RenderPass> getRenderPassRef(void) { return renderPass; }


   private:
    void initSwapchain();

   private:
    VkCommandBuffer getCommandBuffer();
    SDL_Window *window;
    ref<VulkanInstance> vulkan = nullptr;
    ref<RenderPass> renderPass;
    ref<Swapchain> swapchain = nullptr;
  };
}  // namespace ren