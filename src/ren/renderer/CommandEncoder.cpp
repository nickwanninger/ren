#include <ren/renderer/CommandEncoder.h>
#include <ren/renderer/Renderer.h>

namespace ren {


  void CommandEncoder::withRenderPass(RenderPass &pass, RenderTarget &target,
                                      std::function<void(RenderPassEncoder &)> func) {
    RenderPassEncoder encoder(*this, pass, target);
    encoder.begin();
    func(encoder);
    encoder.end();
  }

  void CommandEncoder::copyBuffer(ren::Buffer &src, ren::Buffer &dst, VkDeviceSize size,
                                  VkDeviceSize srcOffset, VkDeviceSize dstOffset) {
    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = srcOffset;
    copyRegion.dstOffset = dstOffset;
    copyRegion.size = size;
    vkCmdCopyBuffer(this->cmd, src.getHandle(), dst.getHandle(), 1, &copyRegion);
  }


  void RenderPassEncoder::setScissor(glm::uvec2 pos, glm::uvec2 size) {
    VkRect2D scissor = {};
    scissor.offset = {static_cast<int32_t>(pos.x), static_cast<int32_t>(pos.y)};
    scissor.extent = {size.x, size.y};
    vkCmdSetScissor(buf(), 0, 1, &scissor);
  }

  void RenderPassEncoder::setViewport(glm::uvec2 pos, glm::uvec2 size) {
    VkViewport viewport = {};
    viewport.x = static_cast<float>(pos.x);
    viewport.y = static_cast<float>(pos.y);
    viewport.width = static_cast<float>(size.x);
    viewport.height = static_cast<float>(size.y);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(buf(), 0, 1, &viewport);
  }


  void RenderPassEncoder::begin() {
    // Nothing to do here; beginRenderPass is handled by CommandEncoder.


    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = pass.getHandle();
    renderPassInfo.framebuffer = target.getHandle(pass);  // grab the framebuffer for this pass.
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {target.getWidth(), target.getHeight()};

    // TODO: allow renderpasses to configure clear values!
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
    vkCmdBeginRenderPass(buf(), &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    setViewport({0, 0}, {target.getWidth(), target.getHeight()});
    setScissor({0, 0}, {target.getWidth(), target.getHeight()});
  }

  void RenderPassEncoder::end() {
    // Simply end the render pass.
    vkCmdEndRenderPass(buf());
  }



  ref<ShaderObject> RenderPassEncoder::bindPipeline(ren::PipelineStateObject &pso) {
    // Bind the pipeline described by the PSO.
    auto obj = pso.program->instantiate();
    bindPipeline(pso, *obj);
    return obj;
  }


  void RenderPassEncoder::bindPipeline(ren::PipelineStateObject &pso, ShaderObject &object) {
    auto &R = ren::Renderer::get();

    auto cachedPipeline = R.getPipelineCache().get(this->pass, pso);

    // // Bind the pipeline described by the PSO.
    vkCmdBindPipeline(buf(), VK_PIPELINE_BIND_POINT_GRAPHICS, cachedPipeline->getHandle());


    auto &sets = object.getDescriptorSets();
    // // Bind descriptor sets from the shader object.
    vkCmdBindDescriptorSets(buf(), VK_PIPELINE_BIND_POINT_GRAPHICS, object.getLayout(), 0,
                            static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);
  }


  void RenderPassEncoder::drawIndexed(DrawArguments &args) {
    vkCmdDrawIndexed(buf(), args.vertexCount, args.instanceCount, args.firstIndex, args.firstVertex,
                     args.firstInstance);
  }

  void RenderPassEncoder::drawFullscreenQuad(void) {
    // Draw a full screen triangle (3 vertices, no vertex buffer)
    vkCmdDraw(buf(), 3, 1, 0, 0);
  }


}  // namespace ren