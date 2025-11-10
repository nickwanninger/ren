#pragma once

#include <ren/renderer/graph/RenderPassTask.h>
#include <ren/renderer/graph/RunContext.h>
#include <ren/renderer/pipelines/PipelineStateObject.h>
#include <ren/renderer/graph/RenderGraph.h>
#include <ren/renderer/Buffer.h>

namespace ren {

  class SSAOTask : public ren::RenderPassTask {
   public:
    ren::PipelineStateObject pso;


    struct SSAOUniform {
      glm::mat4 projection;      // offset 0, 64 bytes
      glm::mat4 inv_projection;  // offset 64, 64 bytes
      glm::vec4 samples[64];     // offset 128, 1024 bytes (each vec3 padded to 16 bytes)
      glm::vec2 screen_size;     // offset 1152, 8 bytes
      float radius;              // offset 1160, 4 bytes
      float bias;                // offset 1164, 4 bytes
      int num_samples;           // offset 1168, 4 bytes
      int _padding;              // offset 1172, 4 bytes (explicit padding)
    };
    UniformBufferSet<SSAOUniform> uSSAO;
    SSAOUniform ssao;

    ref<ren::Image> noiseTexture;

    struct {
      GraphHandle ssao;
    } out;

    struct {
      GraphHandle depth, normal;
    } in;

    SSAOTask(ren::RenderGraph &G, float scale, GraphHandle depthHandle, GraphHandle normalHandle);

    void run(ren::GraphRunContext &ctx) override;

    // inspect
    void inspect(void) override;
  };




  class SSAOBlurTask : public ren::RenderPassTask {
   public:
    ren::PipelineStateObject pso;

    struct {
      GraphHandle ssao;
    } in;

    struct {
      GraphHandle ssao_blurred;
    } out;

    SSAOBlurTask(ren::RenderGraph &G, float scale, GraphHandle ssaoHandle);

    void run(ren::GraphRunContext &ctx) override;
  };


  inline void addSSAO(RenderGraph &G, ren::GraphHandle depthHandle, ren::GraphHandle normalHandle,
                      ren::GraphHandle &ssaoOut) {
    float scale = 0.5f;
    auto &ssao = G.addTask<SSAOTask>("ssao", scale, depthHandle, normalHandle);
    auto &blur = G.addTask<SSAOBlurTask>("ssao_blur", scale, ssao.out.ssao);


    ssaoOut = blur.out.ssao_blurred;
    // ssaoOut = blur.out.ssao_blurred;
  }

}  // namespace ren