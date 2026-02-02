#pragma once

#include <ren/renderer/graph/RenderPassTask.h>
#include <ren/renderer/graph/RunContext.h>
#include <ren/renderer/pipelines/PipelineStateObject.h>
#include <ren/renderer/graph/RenderGraph.h>

namespace ren {

  class DepthPrepassTask : public ren::RenderPassTask {
   public:
    ren::PipelineStateObject pso;
    // The depth texture output from the prepass.
    ren::GraphHandle depthOut;
    // The geometry normal output from the prepass.
    // TODO: should we output samples from normal maps if we have them?
    ren::GraphHandle normalOut;

    DepthPrepassTask(ren::RenderGraph &G);

    void run(ren::GraphRenderPassContext &ctx) override;
  };


  inline void addDepthPrepass(RenderGraph &G, ren::GraphHandle &depthHandle,
                           ren::GraphHandle &normalHandle) {
    auto &dpp = G.addTask<DepthPrepassTask>("depth_prepass");
    depthHandle = dpp.depthOut;
    normalHandle = dpp.normalOut;
  }

}  // namespace ren