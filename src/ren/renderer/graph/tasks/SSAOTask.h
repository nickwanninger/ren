#pragma once

#include <ren/renderer/graph/RenderPassTask.h>
#include <ren/renderer/graph/RunContext.h>
#include <ren/renderer/pipelines/PipelineStateObject.h>
#include <ren/renderer/graph/RenderGraph.h>
#include <ren/renderer/Buffer.h>

namespace ren {

  class Texture;

  // Screen-space ambient occlusion from the G-buffer depth and normals. The
  // shader (ren/core/ssao) is a fullscreen surface. It gets the view and
  // projection from the frame globals and makes its sample kernel itself, so
  // the parameters below are the whole push constant.
  class SSAOTask : public ren::RenderPassTask {
   public:
    ren::PipelineStateObject pso;

    // The view-space radius of the sample hemisphere.
    float radius = 0.5f;
    // Depth bias against self-occlusion on flat surfaces.
    float bias = 0.025f;
    i32 sampleCount = 16;

    // RGB noise that rotates the kernel per pixel.
    ref<ren::Texture> noiseTexture;

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
    void inspect(void) override;
  };


  inline auto &addSSAO(RenderGraph &G, ren::GraphHandle depthHandle, ren::GraphHandle normalHandle,
                       ren::GraphHandle &ssaoOut) {
    auto &ssao = G.addTask<SSAOTask>("ssao", 0.5f, depthHandle, normalHandle);
    // The blur runs at full scale, so it also upsamples the SSAO.
    auto &blur = G.addTask<SSAOBlurTask>("ssao_blur", 1.0f, ssao.out.ssao, depthHandle, normalHandle);
    ssaoOut = blur.out.ssao_blurred;
    return ssao;
  }

}  // namespace ren