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
      glm::mat4 projection;
      glm::mat4 inv_projection;
      glm::mat4 normal_matrix;  // used to convert from world space normals to view space.
      glm::vec4 samples[64];
      glm::vec2 screen_size;
      float radius = 1.0f;
      float intensity = 1.0f;  // WARNING: artistic control! (BAD)
      float bias = 0.025f;
      int num_samples = 12;
    };
    SSAOUniform ssao;

    ref<ren::Image> noiseTexture;

    struct {
      GraphHandle ssao;
    } out;

    struct {
      GraphHandle depth, normal;
    } in;

    SSAOTask(ren::RenderGraph &G, float scale, GraphHandle depthHandle, GraphHandle normalHandle);

    void run(ren::GraphRenderPassContext &ctx) override;

    // inspect
    void inspect(void) override;
  };




  class SSAOBlurTask : public ren::RenderPassTask {
   public:
    ren::PipelineStateObject pso;

    struct In {
      GraphHandle ssao;
      GraphHandle depth, normal; // For joint bilateral blur
    } in;

    struct Out {
      GraphHandle ssao_blurred;
    } out;

    SSAOBlurTask(ren::RenderGraph &G, float scale, GraphHandle ssaoHandle, GraphHandle depthHandle,
                 GraphHandle normalHandle);

    void run(ren::GraphRenderPassContext &ctx) override;
  };


  inline auto &addSSAO(RenderGraph &G, ren::GraphHandle depthHandle, ren::GraphHandle normalHandle,
                       ren::GraphHandle &ssaoOut) {
    auto &ssao = G.addTask<SSAOTask>("ssao", 0.5f, depthHandle, normalHandle);
    auto &blur = G.addTask<SSAOBlurTask>("ssao_blur", 1.0f, ssao.out.ssao, depthHandle, normalHandle);

    // Output the blurred SSAO
    ssaoOut = blur.out.ssao_blurred;
    // ssaoOut = ssao.out.ssao;

    return ssao;  // TEMP
  }

}  // namespace ren