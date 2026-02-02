#include <ren/renderer/CommandEncoder.h>
#include <ren/renderer/Renderer.h>
#include <ren/renderer/submission/SubmissionUnit.h>

namespace ren {


  void CommandEncoder::withRenderPass(RenderPass &pass, RenderTarget &target, std::function<void(RenderPassEncoder &)> func) {
    RenderPassEncoder encoder = beginRenderPass(pass, target);
    func(encoder);
    encoder.end();
  }

  RenderPassEncoder CommandEncoder::beginRenderPass(RenderPass &pass, RenderTarget &target) {
    RenderPassEncoder encoder(*this, pass, target);
    encoder.begin();
    return encoder;
  }

  void CommandEncoder::copyBuffer(ren::Buffer &src, ren::Buffer &dst, VkDeviceSize size, VkDeviceSize srcOffset, VkDeviceSize dstOffset) {
    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = srcOffset;
    copyRegion.dstOffset = dstOffset;
    copyRegion.size = size;
    vkCmdCopyBuffer(this->cmd, src.getHandle(), dst.getHandle(), 1, &copyRegion);
  }

  void CommandEncoder::dispatchCompute(ShaderObject &shader, glm::uvec3 groupCount) {
    auto &R = ren::Renderer::get();
    auto pipeline = R.getPipelineCache().getCompute(shader.program);

    vkCmdBindPipeline(this->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->getHandle());

    shader.bind(this->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->getLayout());

    vkCmdDispatch(this->cmd, groupCount.x, groupCount.y, groupCount.z);
  }


  void CommandEncoder::reset(void) {
    // Nothing to do here - resources are managed by SubmissionUnit
  }


  CommandEncoder::QueryTicket CommandEncoder::beginTimestampQuery(const char *logical_name) {
    auto *q = submissionUnit.newQuery(logical_name);
    vkCmdWriteTimestamp(this->cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, submissionUnit.getQueryPool(), q->startIndex);
    return q->endIndex;
  }


  void CommandEncoder::endTimestampQuery(QueryTicket ticket) {
    vkCmdWriteTimestamp(this->cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, submissionUnit.getQueryPool(), ticket);
  }


  ren::Arena &CommandEncoder::getArena(void) { return submissionUnit.getArena(); }


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
        if (attachment.format != VK_FORMAT_D32_SFLOAT && attachment.format != VK_FORMAT_D24_UNORM_S8_UINT) {
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
    // TODO:
    ref<ShaderObject> obj = ren::make<ShaderObject>(pso.program, getSubmissionUnit());
    bindPipeline(pso, *obj);
    return obj;
  }


  void RenderPassEncoder::bindPipeline(ren::PipelineStateObject &pso, ShaderObject &object) {
    auto &R = ren::Renderer::get();

    auto cachedPipeline = R.getPipelineCache().get(this->pass, pso);

    // // Bind the pipeline described by the PSO.
    vkCmdBindPipeline(buf(), VK_PIPELINE_BIND_POINT_GRAPHICS, cachedPipeline->getHandle());
  }


  void RenderPassEncoder::bindImmediateMesh(std::span<ren::Vertex> vertices, std::span<u32> indices) {
    // Create vertex buffer
    auto vbuf = getEncoder().getArena().push<ren::VertexBuffer<ren::Vertex>>(sizeof(ren::Vertex) * static_cast<VkDeviceSize>(vertices.size()));
    vbuf->map();
    std::memcpy(vbuf->map(), vertices.data(), sizeof(ren::Vertex) * vertices.size());
    vbuf->unmap();

    // Create index buffer
    auto ibuf = getEncoder().getArena().push<ren::IndexBuffer>(sizeof(u32) * static_cast<VkDeviceSize>(indices.size()));
    ibuf->map();
    std::memcpy(ibuf->map(), indices.data(), sizeof(u32) * indices.size());
    ibuf->unmap();

    VkDeviceSize offsets[] = {0};
    VkBuffer vbufHandle = vbuf->getHandle();
    vkCmdBindVertexBuffers(buf(), 0, 1, &vbufHandle, offsets);
    vkCmdBindIndexBuffer(buf(), ibuf->getHandle(), 0, VK_INDEX_TYPE_UINT32);
  }


  void RenderPassEncoder::drawIndexed(const DrawArguments &args) {
    vkCmdDrawIndexed(buf(), args.vertexCount, args.instanceCount, args.firstIndex, args.firstVertex, args.firstInstance);
  }

  void RenderPassEncoder::drawFullscreenQuad(void) {
    // Draw a full screen triangle (3 vertices, no vertex buffer)
    vkCmdDraw(buf(), 3, 1, 0, 0);
  }


}  // namespace ren
