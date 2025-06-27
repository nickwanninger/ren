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


    RenderPass::Description rpDesc;
    rpDesc.addColor(vulkan->swapchainFormat);
    rpDesc.addDepth();

    // Create the render pass.
    this->renderPass = makeRef<ren::RenderPass>(rpDesc);
    fmt::println("Render Pass created with handle: {}", (void *)this->renderPass.get());

    initSwapchain();
  }

  Renderer::~Renderer(void) {
    REN_PROFILE_FUNCTION();
    waitForIdle();

    this->swapchain.reset();
    this->renderPass.reset();
    this->vulkan.reset();
  }

  void Renderer::waitForIdle(void) {
    REN_PROFILE_FUNCTION();
    this->vulkan->waitForIdle();
  }


  void Renderer::beginPass(ren::RenderPass &pass, ren::RenderTarget &target) {
    REN_PROFILE_FUNCTION();

    auto &frame = ren::getFrameData();
    auto cmd = getCommandBuffer();

    // Begin the render pass.
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = pass.getHandle();
    renderPassInfo.framebuffer = target.getHandle();
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {target.getWidth(), target.getHeight()};

    // TODO: figure out if we need to clear the render targets
    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{1.0f, 1.0f, 1.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

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

  VkCommandBuffer Renderer::getCommandBuffer() { return getFrameData().commandBuffer; }

  void Renderer::beginFrame(void) {
    REN_PROFILE_FUNCTION();

    ren::FrameData *frame = nullptr;

    do {
      frame = this->swapchain->acquireNextFrame();
      if (frame == nullptr) {
        // The swapchain is out of date, so we need to recreate it.
        this->initSwapchain();
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