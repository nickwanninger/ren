#include <ren/renderer/Renderer.h>




namespace ren {


  static Renderer *g_renderer = nullptr;
  Renderer &Renderer::get(void) {
    if (g_renderer == nullptr) { throw std::runtime_error("Renderer not initialized"); }
    return *g_renderer;
  }


  Renderer::Renderer(SDL_Window *window)
      : window(window) {
    REN_PROFILE_FUNCTION();

    g_renderer = this;

    // Create the Vulkan instance
    this->vulkan = makeRef<VulkanInstance>(this->window);


    initSwapchain();
  }

  Renderer::~Renderer(void) {
    REN_PROFILE_FUNCTION();
    waitForIdle();

    // For various reasons, we want to explicitly clear the render pass cache
    // before destroying the vulkan instance or anything else.
    this->renderPassCache.clearCache();

    this->swapchain.reset();
    this->vulkan.reset();
  }

  void Renderer::waitForIdle(void) {
    REN_PROFILE_FUNCTION();
    this->vulkan->waitForIdle();
  }


  void Renderer::beginPass(ren::RenderPass &pass, ren::RenderTarget &target) {
    REN_PROFILE_FUNCTION();

    this->currentPass = pass.shared_from_this();

    auto &frame = ren::getFrameData();
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
    auto &frame = ren::getFrameData();
    auto cmd = getCommandBuffer();

    assert(this->currentPass != nullptr &&
           "Cannot bind a pipeline without a current render pass set. Call beginPass first.");

    // Get the cached pipeline for this render pass and PSO.
    auto newPipeline = this->pipelineCache.get(*this->currentPass, pso);

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
  }



  ShaderBinder Renderer::startBinding(u32 set) {
    REN_PROFILE_FUNCTION();

    // Start binding the shader program.
    if (this->currentPipeline == nullptr) {
      throw std::runtime_error(
          "Cannot start binding without a current pipeline set. Call bind() first.");
    }

    // Create a shader binder for the current program.
    return ShaderBinder(*getCurrentProgram(), set);
  }


  void Renderer::bind(ref<ShaderProgram> program) {
    REN_PROFILE_FUNCTION();

    // Bind the shader program.
    if (program == nullptr) { throw std::runtime_error("Cannot bind a null shader program."); }

    // allocate a temporary pso.
    PipelineStateObject pso;
    pso.program = program;
    bind(pso);
  }

  VkCommandBuffer Renderer::getCommandBuffer() { return getFrameData().commandBuffer; }

  void Renderer::beginFrame(void) {
    REN_PROFILE_FUNCTION();

    this->currentPass = nullptr;
    this->currentPipeline = nullptr;

    ren::FrameData *frame = nullptr;


    // check if the SDL window is a different size than the swapchain.
    int width, height;
    SDL_Vulkan_GetDrawableSize(this->window, &width, &height);

    // bool sizeIncorrect = false;
    // if (width != (int)this->swapchain->deviceExtent.width ||
    //     height != (int)this->swapchain->deviceExtent.height) {
    //       sizeIncorrect = true;
    //   fmt::println("Window resized from {}x{} to {}x{}. Swapchain invalid!",
    //                this->swapchain->deviceExtent.width, this->swapchain->deviceExtent.height,
    //                width, height);
    // }

    do {
      frame = this->swapchain->acquireNextFrame();

      if (frame == nullptr) {
        // The swapchain is out of date, so we need to recreate it.
        this->initSwapchain();
        SDL_Delay(100);
        ren::getVulkan().waitForIdle();
      }
    } while (frame == nullptr);


    vulkan->frame_number += 1;

    // Initialize the frame's command buffer.

    auto cmd = frame->commandBuffer;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    beginInfo.pInheritanceInfo = nullptr;

    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
      throw std::runtime_error("failed to begin recording command buffer!");
    }


    // Clear the descriptor pool for this frame to make space for new descriptor sets.
    frame->descriptorAllocator.reset_pools();
  }

  void Renderer::endFrame(void) {
    // End the display pass.
    REN_PROFILE_FUNCTION();

    auto &frame = ren::getFrameData();

    // And we've finished recording the command buffer:
    if (vkEndCommandBuffer(frame.commandBuffer) != VK_SUCCESS) {
      throw std::runtime_error("failed to record command buffer!");
    }




    // TODO: abstract all this.
    VkSemaphore signalSemaphores[] = {frame.renderFinishedSemaphore};

    {
      REN_PROFILE_SCOPE("Submit Graphics Queue");
      VkSubmitInfo submitInfo{};
      submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

      VkSemaphore waitSemaphores[] = {frame.imageAvailableSemaphore};
      VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
      submitInfo.waitSemaphoreCount = 1;
      submitInfo.pWaitSemaphores = waitSemaphores;
      submitInfo.pWaitDstStageMask = waitStages;

      submitInfo.commandBufferCount = 1;
      submitInfo.pCommandBuffers = &frame.commandBuffer;

      submitInfo.signalSemaphoreCount = 1;
      submitInfo.pSignalSemaphores = signalSemaphores;

      if (vkQueueSubmit(vulkan->graphics_queue, 1, &submitInfo, frame.inFlightFence) !=
          VK_SUCCESS) {
        throw std::runtime_error("failed to submit draw command buffer!");
      }
    }


    {
      REN_PROFILE_SCOPE("Presentation");

      // Presentation
      VkPresentInfoKHR presentInfo{};
      presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

      presentInfo.waitSemaphoreCount = 1;
      presentInfo.pWaitSemaphores = signalSemaphores;

      VkSwapchainKHR swapChains[] = {swapchain->swapchain};
      presentInfo.swapchainCount = 1;
      presentInfo.pSwapchains = swapChains;
      uint32_t index = frame.frameIndex;  // we need a u32
      presentInfo.pImageIndices = &index;

      presentInfo.pResults = nullptr;  // Optional
      vkQueuePresentKHR(vulkan->graphics_queue, &presentInfo);
    }
  }


  void Renderer::initSwapchain(void) {
    REN_PROFILE_FUNCTION();

    // wait for the GPU to be idle.
    this->vulkan->waitForIdle();
    this->swapchain.reset();

    this->swapchain = makeBox<ren::Swapchain>(this->window);
  }

}  // namespace ren