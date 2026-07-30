#pragma once

#include <ren/types.h>
#include <ren/core/Arena.h>
#include <ren/renderer/Buffer.h>
#include <ren/renderer/shader/ShaderProgram.h>
#include <ren/renderer/RenderPass.h>
#include <ren/renderer/RenderTarget.h>
#include <ren/assets/Vertex.h>
#include <ren/renderer/shader/ShaderCursor.h>
#include <ren/renderer/pipelines/PipelineStateObject.h>
#include <functional>

namespace ren {


  class SubmissionUnit;  // forward declaration

  /**
   * The theory of operation for rendering in the REN engine is as follows:
   * 1. You call beginFrame() on the renderer to start a new frame, which returns a CommandEncoder.
   * 2. You use the CommandEncoder to record rendering commands.
   *   - You can create RenderPassEncoders to record render passes.
   *   - with a RenderPassEncoder, you use a PipelineStateObject to bind a pipeline which returns a
   *     ShaderCursor.
   *   - A ShaderCursor writes reflected push-constant fields for the currently
   *     bound program. Global descriptor sets are always bound by the encoder.
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
    CommandEncoder(VkCommandBuffer cmdBuf, SubmissionUnit &submissionUnit)
        : cmd(cmdBuf)
        , submissionUnit(submissionUnit) {}



    void withRenderPass(RenderPass &pass, RenderTarget &target, std::function<void(RenderPassEncoder &)> func);
    RenderPassEncoder beginRenderPass(RenderPass &pass, RenderTarget &target);
    // TODO:
    // ComputePassEncoder *beginComputePass();

    ShaderCursor bindCompute(ref<ShaderProgram> program);
    void dispatch(const ShaderCursor &cursor, glm::uvec3 groupCount);

    void copyBuffer(ren::Buffer &src, ren::Buffer &dst, VkDeviceSize size, VkDeviceSize srcOffset = 0, VkDeviceSize dstOffset = 0);


    // TODO:
    // - copyTexture
    // - copyTextureToBuffer
    // - copyBufferToTexture
    // - uploadTextureData
    // - uploadBufferData
    // - clearBuffer


    // TODO: Abstraction leakage!
    VkCommandBuffer buf() const { return cmd; }

    SubmissionUnit &getSubmissionUnit(void) { return submissionUnit; }
    ren::Arena &getArena(void);

    // Reset the command encoder for reuse.
    void reset(void);

    void writePushConstant(
        const ShaderCursor& cursor,
        std::string_view name,
        const void* data,
        size_t size);


    // Call beginTimestampQuery before some section of GPU work, then call
    // endTimestampQuery after it.  The timestamps can be resolved after GPU
    // execution. Eventually, this information is collected and reported back to
    // the CPU and is associated with the logical name provided.
    using QueryTicket = u32;
    QueryTicket beginTimestampQuery(const char *logical_name);
    void endTimestampQuery(QueryTicket ticket);

   private:
    friend class RenderPassEncoder;
    ShaderCursor activateGraphics(
        ref<ShaderProgram> program, VkPipelineLayout pipelineLayout);
    void validate(
        const ShaderCursor& cursor, VkPipelineBindPoint expectedBindPoint) const;

    struct BoundShader {
      ref<ShaderProgram> program;
      VkPipelineLayout layout = VK_NULL_HANDLE;
      u64 generation = 0;
    };

    VkCommandBuffer cmd;
    SubmissionUnit &submissionUnit;
    BoundShader graphics;
    BoundShader compute;
  };


  class SubEncoder {
   public:
    SubEncoder(CommandEncoder &cmd)
        : cmd(cmd) {}
    virtual ~SubEncoder() = default;

    inline CommandEncoder &getEncoder(void) { return cmd; }
    inline SubmissionUnit &getSubmissionUnit(void) { return cmd.getSubmissionUnit(); }
    // TODO: Abstraction leakage!
    inline VkCommandBuffer buf() { return getEncoder().buf(); }

    inline ren::Arena &getArena(void) { return cmd.getArena(); }

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


    ShaderCursor bindGraphics(ren::PipelineStateObject &pso);

    void bindImmediateMesh(std::span<ren::Vertex> vertices, std::span<u32> indices);

    void setScissor(glm::uvec2 pos, glm::uvec2 size);
    void setViewport(glm::uvec2 pos, glm::uvec2 size);


    void drawIndexed(const ShaderCursor &cursor, const DrawArguments &args);
    void drawFullscreenTriangle(const ShaderCursor &cursor);


    // End the render pass.
    void end();

   protected:
    friend class CommandEncoder;
    void begin();

   private:
    RenderPass &pass;
    RenderTarget &target;
  };
}  // namespace ren
