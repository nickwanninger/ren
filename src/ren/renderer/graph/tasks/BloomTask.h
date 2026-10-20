#pragma once

#include <ren/renderer/graph/RenderGraph.h>
#include <ren/renderer/graph/RenderPassTask.h>
#include <ren/renderer/pipelines/PipelineStateObject.h>

namespace ren {

  // One level of the HDR bloom pyramid. Each pass writes a separate image.
  class BloomFilterTask : public RenderPassTask {
   public:
    PipelineStateObject pso;
    GraphHandle source, detail, output;
    bool upsample;
    bool prefilter;
    float threshold = 1.0f;

    BloomFilterTask(RenderGraph &graph, float scale, GraphHandle source, GraphHandle detail, bool upsample, bool prefilter);
    void run(GraphRenderPassContext &ctx) override;
    void inspect() override;
  };

  class BloomCompositeTask : public RenderPassTask {
   public:
    PipelineStateObject pso;
    GraphHandle scene, bloom, output;
    BloomFilterTask *prefilter = nullptr;
    float strength = 0.1f;
    float exposure = 0.0f;
    bool bypass = false;

    BloomCompositeTask(RenderGraph &graph, GraphHandle scene, GraphHandle bloom);
    void run(GraphRenderPassContext &ctx) override;
    void inspect() override;
  };

  // Returns the final pass for controls, as addSSAO returns its SSAO task.
  BloomCompositeTask &addBloom(RenderGraph &graph, GraphHandle scene, GraphHandle &output);

}  // namespace ren
