#pragma once

#include <ren/types.h>
#include <ren/core/Arena.h>
#include <ren/renderer/Buffer.h>
#include <ren/renderer/shader/ShaderProgram.h>
#include <ren/renderer/RenderPass.h>
#include <ren/renderer/RenderTarget.h>
#include <ren/assets/Vertex.h>
#include <ren/renderer/pipelines/PipelineStateObject.h>
#include <functional>

namespace ren {

  /**
   * The theory of operation for rendering in the REN engine is as follows:
   * 1. You call beginFrame() on the renderer to start a new frame, which returns a CommandEncoder.
   * 2. You use the CommandEncoder to record rendering commands.
   *   - You can create RenderPassEncoders to record render passes.
   *   - with a RenderPassEncoder, you use a PipelineStateObject to bind a pipeline which returns a
   *     ShaderObject.
   *   - A ShaderObject represents an instance of the shader program with its own descriptor sets
   *     and resource usages. You use a shader object to bind resources (textures, buffers) to the
   *     pipeline.
   *   - Optionally, you can provide your own ShaderObject.
   * 4. You call endFrame() on the renderer to submit the recorded commands for execution.
   */

  class RenderPassEncoder;
  class ComputePassEncoder;


  // This class abstracts a command buffer for recording rendering commands.
  // CommandEncoders exist as a higher-level interface for recording commands.
  class CommandBuffer {
   public:
    virtual ~CommandBuffer() = default;
  };



  // A command encoder is used to record commands into a command buffer.
  // Currently, this just wraps a VkCommandBuffer, but one day it'll be the
  // basis for an RHI interface.
  class CommandEncoder {
   public:
    CommandEncoder(VkCommandBuffer cmdBuf)
        : cmd(cmdBuf) {}



    void withRenderPass(RenderPass &pass, RenderTarget &target,
                        std::function<void(RenderPassEncoder &)> func);
    RenderPassEncoder beginRenderPass(RenderPass &pass, RenderTarget &target);
    // TODO:
    // ComputePassEncoder *beginComputePass();


    void copyBuffer(ren::Buffer &src, ren::Buffer &dst, VkDeviceSize size,
                    VkDeviceSize srcOffset = 0, VkDeviceSize dstOffset = 0);

    // TODO:
    // - copyTexture
    // - copyTextureToBuffer
    // - copyBufferToTexture
    // - uploadTextureData
    // - uploadBufferData
    // - clearBuffer


    // TODO: Abstraction leakage!
    VkCommandBuffer buf() const { return cmd; }

    ren::Arena &getArena(void) { return arena; }

    // Reset the command encoder for reuse.
    void reset(void);

   private:
    VkCommandBuffer cmd;
    ren::Arena arena;
  };


  class SubEncoder {
   public:
    SubEncoder(CommandEncoder &cmd)
        : cmd(cmd) {}
    virtual ~SubEncoder() = default;

    inline CommandEncoder &getEncoder(void) { return cmd; }
    // TODO: Abstraction leakage!
    inline VkCommandBuffer buf() { return getEncoder().buf(); }


    ren::Arena &getArena(void) { return cmd.getArena(); }


   protected:
    CommandEncoder &cmd;
  };



  struct DrawArguments {
    u32 vertexCount = 0;
    u32 instanceCount = 1;
    u32 firstVertex = 0;
    u32 firstInstance = 0;
    u32 firstIndex = 0;
  };

  class RenderPassEncoder : public SubEncoder {
   public:
    RenderPassEncoder(CommandEncoder &cmd, RenderPass &pass, RenderTarget &target)
        : SubEncoder(cmd)
        , pass(pass)
        , target(target) {}
    ~RenderPassEncoder() = default;


    ref<ShaderObject> bindPipeline(ren::PipelineStateObject &pso);
    void bindPipeline(ren::PipelineStateObject &pso, ShaderObject &object);

    void bindImmediateMesh(std::span<ren::Vertex> vertices, std::span<u32> indices);

    void setScissor(glm::uvec2 pos, glm::uvec2 size);
    void setViewport(glm::uvec2 pos, glm::uvec2 size);


    void drawIndexed(const DrawArguments &args);
    // Issue a full screen triangle draw call (for blit and post-processes)
    void drawFullscreenQuad(void);


    // End the render pass.
    void end();

   protected:
    friend class CommandEncoder;
    void begin();

   private:
    RenderPass &pass;
    RenderTarget &target;
  };

  class ComputePassEncoder : public SubEncoder {};
}  // namespace ren
