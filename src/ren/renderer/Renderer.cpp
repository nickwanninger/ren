#include <ren/renderer/Renderer.h>
#include <ren/misc/DeprecationLogger.h>
#include <ren/renderer/CommandEncoder.h>
#include <ren/renderer/submission/SubmissionQueue.h>
#include <ren/core/Window.h>

namespace ren {

  static Renderer *g_renderer = nullptr;
  static FrameContext *g_frameUnit = nullptr;

  FrameContext &getFrameUnit(void) {
    if (!g_frameUnit) {
      throw std::runtime_error("No current frame. Call Renderer::beginFrame() first.");
    }
    return *g_frameUnit;
  }

  Renderer &Renderer::get(void) {
    if (g_renderer == nullptr) { throw std::runtime_error("Renderer not initialized"); }
    return *g_renderer;
  }


  Renderer::Renderer(ref<VulkanInstance> vulkan)
      : vulkan(std::move(vulkan)) {
    REN_PROFILE_FUNCTION();

    g_renderer = this;

    if (!this->vulkan->hasDevice()) {
      throw std::runtime_error("Renderer needs a Vulkan device. Call VulkanInstance::selectDevice() first.");
    }
    this->imageHeap = make<ImageDescriptorHeap>();
    this->samplerHeap = make<SamplerDescriptorHeap>();
    this->samplerCache = make<SamplerCache>(this->samplerHeap);
    this->frameGlobalsAllocator = makeBox<FrameGlobalsAllocator>();

    // Frame contexts allocate their frame globals from the allocator above.
    for (u32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      this->frames.push_back(makeBox<FrameContext>(i));
    }
  }

  Renderer::~Renderer(void) {
    REN_PROFILE_FUNCTION();
    waitForIdle();

    g_frameUnit = nullptr;
    this->frames.clear();

    // For various reasons, we want to explicitly clear the render pass cache
    // before destroying the vulkan instance or anything else.
    this->currentPipeline.reset();
    this->currentPass.reset();
    this->displayPass.reset();
    this->renderPassCache.clearCache();

    this->pipelineCache.reset();
    this->shaderCache.reset();
    this->samplerCache.reset();
    this->imageHeap.reset();
    this->samplerHeap.reset();
    this->frameGlobalsAllocator.reset();
    this->vulkan.reset();
  }

  void Renderer::waitForIdle(void) {
    REN_PROFILE_FUNCTION();
    this->vulkan->waitForIdle();
  }


  void Renderer::beginPass(ren::RenderPass &pass, ren::RenderTarget &target) {
    REN_DEPRECATION_WARNING();
    REN_PROFILE_FUNCTION();

    this->currentPass = pass.shared_from_this();

    auto cmd = getCommandBuffer();

    // Begin the render pass.
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = pass.getHandle();
    renderPassInfo.framebuffer = target.getHandle(pass);  // grab the framebuffer for this pass.
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {target.getWidth(), target.getHeight()};

    std::vector<VkClearValue> clearValues;

    for (const auto &attachment : pass.getDescription().attachments) {
      if (attachment.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR) {
        // If the attachment is a color attachment, clear it to white.
        if (attachment.format != VK_FORMAT_D32_SFLOAT &&
            attachment.format != VK_FORMAT_D24_UNORM_S8_UINT) {
          clearValues.push_back({.color = {{0.0f, 0.0f, 0.0f, 0.0f}}});
        } else {
          // Otherwise, it's a depth attachment, clear it to 1.0f.
          clearValues.push_back({.depthStencil = {1.0f, 0}});
        }
      } else {
        // If the attachment is not cleared, we don't need to specify a clear value.
        clearValues.push_back({});
      }
    }


    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    // Issue the command to begin the render pass.
    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // oh.. do this stuff too.
    VkViewport viewport = {0.0f,
                           0.0f,  // x, y
                           (float)target.getWidth(),
                           (float)target.getHeight(),
                           0.0f,
                           1.0f};
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {{0, 0},
                        {
                            target.getWidth(),
                            target.getHeight(),
                        }};
    vkCmdSetScissor(cmd, 0, 1, &scissor);
  }

  void Renderer::endPass(void) {
    REN_PROFILE_FUNCTION();
    // End the render pass.
    vkCmdEndRenderPass(getCommandBuffer());
  }


  void Renderer::bind(const ren::PipelineStateObject &pso) {
    REN_PROFILE_FUNCTION();

    // Bind the pipeline state object.
    auto &frame = ren::getFrameUnit();
    auto cmd = getCommandBuffer();

    assert(this->currentPass != nullptr &&
           "Cannot bind a pipeline without a current render pass set. Call beginPass first.");

    // Get the cached pipeline for this render pass and PSO.
    auto newPipeline = getPipelineCache().get(*this->currentPass, pso);

    if (newPipeline == nullptr) {
      throw std::runtime_error("Failed to get cached pipeline for PSO: " + pso.debugName);
    }
    if (newPipeline == currentPipeline) {
      // Don't rebind if the pipeline is already bound.
      return;
    }

    this->currentPipeline = newPipeline;

    // printf("Binding PSO: %s (%p)\n", pso.debugName.c_str(), currentPipeline->getHandle());

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, currentPipeline->getHandle());
    std::array sets{
        frame.getFrameGlobalsSet(), imageHeap->getSet(), samplerHeap->getSet()};
    vkCmdBindDescriptorSets(
        cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, currentPipeline->getLayout(),
        ShaderABI::frameSet, static_cast<u32>(sets.size()),
        sets.data(), 0, nullptr);
  }

  void Renderer::bind(ref<ShaderProgram> program) {
    // Bind the shader program.
    if (program == nullptr) { throw std::runtime_error("Cannot bind a null shader program."); }

    // allocate a temporary pso.
    PipelineStateObject pso;
    pso.program = program;
    bind(pso);
  }

  VkCommandBuffer Renderer::getCommandBuffer() { return getFrameUnit().getMainCommandEncoder()->buf(); }

  void Renderer::beginFrame(Window &window) {
    REN_PROFILE_FUNCTION();

    this->currentPass = nullptr;
    this->currentPipeline = nullptr;

    const u32 slot = vulkan->frame_number % frames.size();
    auto &frame = *frames[slot];

    // Wait for this slot's previous work before we reuse its resources. This
    // also frees the acquire semaphore of this slot in every swapchain.
    frame.waitForFence();

    bool acquired = false;
    for (int i = 0; i < 10 && !acquired; i++) {
      REN_PROFILE_SCOPE("Acquire Next Frame");
      acquired = window.getSwapchain().acquire(slot);

      if (!acquired) {
        // Swapchain is out of date, rebuild it.
        window.rebuildSwapchain();
      }
    }

    if (!acquired) {
      throw std::runtime_error("Failed to acquire next frame from swapchain.");
    }

    g_frameUnit = &frame;
    vulkan->frame_number += 1;

    frame.beginFrame();
  }

  void Renderer::endFrame(Window &window) {

    auto &frame = ren::getFrameUnit();
    auto &swapchain = window.getSwapchain();

    VkSemaphore waitSems[] = {swapchain.currentImageAvailable()};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore signalSems[] = {swapchain.currentRenderDone()};

    frame.submitFrame(*vulkan->graphicsQueue, {
                                                  .waitSemaphores = waitSems,
                                                  .waitStages = waitStages,
                                                  .signalSemaphores = signalSems,
                                              });

    {
      VkSwapchainKHR swapchains[] = {swapchain.swapchain};
      u32 imageIndices[] = {swapchain.currentImageIndex()};

      VkPresentInfoKHR presentInfo{};
      presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
      presentInfo.waitSemaphoreCount = 1;
      presentInfo.pWaitSemaphores = signalSems;
      presentInfo.swapchainCount = 1;
      presentInfo.pSwapchains = swapchains;
      presentInfo.pImageIndices = imageIndices;

      auto result = vkQueuePresentKHR(vulkan->graphicsQueue->getHandle(), &presentInfo);
      // A resize (or going fullscreen) makes the swapchain out of date. That's
      // not an error: the next acquire will report it and rebuild.
      if (result != VK_ERROR_OUT_OF_DATE_KHR && result != VK_SUBOPTIMAL_KHR) {
        VK_CHECK(result);
      }
    }
  }


}  // namespace ren
