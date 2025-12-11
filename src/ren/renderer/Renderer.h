#pragma once


#include <ren/types.h>
#include <ren/renderer/Swapchain.h>
#include <ren/renderer/RenderPass.h>
#include <ren/renderer/Image.h>
#include <ren/renderer/Texture.h>
#include <ren/renderer/Vulkan.h>
#include <ren/renderer/RenderPassCache.h>
#include <ren/renderer/pipelines/PipelineCache.h>
#include <ren/renderer/pipelines/PipelineStateObject.h>
#include <ren/renderer/ShaderBinder.h>
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
    // Non-copyable, non-movable
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;
    Renderer(Renderer &&) = delete;
    Renderer &operator=(Renderer &&) = delete;



    // Begin a new frame. This will drive the swapchain and prepare the frame data for drawing.
    void beginFrame(void);
    // End the current frame. This will submit the command buffer to the GPU and
    // present the swapchain image to the screen.

    void endFrame(void);

    // Wait for the GPU to finish all commands before proceeding.
    // Avoid using this as much as you can!
    void waitForIdle(void);

    // Given a render pass and a render target, begin a new render pass.
    void beginPass(ren::RenderPass &pass, ren::RenderTarget &target);
    void endPass(void);

    // Given a render pass and render target, execute a function within that pass.
    template <typename Fn>
    inline void withPass(ren::RenderPass &pass, ren::RenderTarget &target, Fn &&func) {
      REN_PROFILE_FUNCTION();
      beginPass(pass, target);
      func();
      endPass();
    }

    // Once you have a render pass set, you can bind a pipeline state object to
    // it to begin rasterizing geometry however the PSO + Program describes.
    void bind(const ren::PipelineStateObject &pso);
    void bind(ref<ShaderProgram> program);

    // Push constants to the current state.
    template <typename T>
    void setPushConstants(const T &data) {
      vkCmdPushConstants(getCommandBuffer(), currentPipeline->getLayout(),
                         VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(T), (void *)&data);
    }


    // Get the current renderer instance.
    static Renderer &get(void);

    // Get a handle to the render pass cache.
    auto &getRenderPassCache() { return renderPassCache; }
    auto &getPipelineCache() { return pipelineCache; }

    inline auto getDisplayPass() {
      if (displayPass == nullptr) {
        // If the display pass is not initialized, create it.
        auto &vk = ren::getVulkan();
        RenderPass::Description displayPassDesc;
        displayPassDesc.name = "Backbuffer Pass";
        // If MSAA enabled, add a multisample color + resolve to swapchain
        if (vk.msaaSamples != VK_SAMPLE_COUNT_1_BIT) {
          // First attachment: multisample color (not presentable)
          auto &msaa = displayPassDesc.addColorAttachment("backbuffer_msaa", vk.swapchainFormat, vk.msaaSamples);
          msaa.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
          msaa.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
          // Second attachment: resolve to the presentable swapchain image
          auto &resolve = displayPassDesc.addColorAttachment("backbuffer", vk.swapchainFormat, VK_SAMPLE_COUNT_1_BIT);
          resolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
          resolve.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
          // Depth with same sample count as msaa color
          displayPassDesc.addDepthAttachment("backbuffer_depth", vk.msaaSamples);
        } else {
          displayPassDesc.addColorAttachment("backbuffer", vk.swapchainFormat, VK_SAMPLE_COUNT_1_BIT);
          displayPassDesc.addDepthAttachment("backbuffer_depth", VK_SAMPLE_COUNT_1_BIT);
        }
        displayPass = renderPassCache.get(displayPassDesc);
      }
      return displayPass;
    }


    ShaderBinder startBinding(u32 set);


    // TODO: move me to .cpp
    inline Sampler &getSampler(VkFilter filter = VK_FILTER_NEAREST) {
      // Get or create a sampler with the given filter.
      auto it = samplers.find(filter);
      if (it != samplers.end()) {
        return *it->second;
      } else {
        samplers.insert({filter, make<Sampler>(filter)});
        return *samplers[filter];
      }
    }


    const Swapchain &getSwapchain(void) const { return *swapchain; }

   private:
    void initSwapchain();
    inline const PipelineStateObject &getCurrentPSO() const {
      if (currentPipeline == nullptr) {
        throw std::runtime_error("No current pipeline set. Call bind() first.");
      }
      return currentPipeline->getPSO();
    }

    inline auto getCurrentProgram() const { return getCurrentPSO().program; }

   private:
    ren::PipelineCache pipelineCache;
    ref<RenderPass> currentPass = nullptr;
    ref<CachedPipeline> currentPipeline = nullptr;

    VkCommandBuffer getCommandBuffer();
    SDL_Window *window;
    ren::RenderPassCache renderPassCache;
    ref<VulkanInstance> vulkan = nullptr;
    ref<Swapchain> swapchain = nullptr;
    ref<RenderPass> displayPass = nullptr;

    std::unordered_map<VkFilter, ref<Sampler>> samplers;
  };
}  // namespace ren
