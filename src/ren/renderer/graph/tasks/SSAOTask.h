#pragma once

#include <ren/renderer/graph/RenderPassTask.h>
#include <ren/renderer/graph/RunContext.h>
#include <ren/renderer/pipelines/PipelineStateObject.h>
#include <ren/renderer/graph/RenderGraph.h>

namespace ren {

  class SSAOTask : public ren::RenderPassTask {
   public:
    ren::PipelineStateObject pso;
    // The depth texture output from the prepass.

    struct {
      GraphHandle ssao;
    } out;

    struct {
      GraphHandle depth, normal;
    } in;

    SSAOTask(ren::RenderGraph &G, GraphHandle depthHandle, GraphHandle normalHandle);

    void run(ren::GraphRunContext &ctx) override;
  };


  inline void addSSAO(RenderGraph &G, ren::GraphHandle depthHandle, ren::GraphHandle normalHandle,
                      ren::GraphHandle &ssaoOut) {
    auto &pass = G.addTask<SSAOTask>("ssao", depthHandle, normalHandle);

    ssaoOut = pass.out.ssao;
  }

}  // namespace ren