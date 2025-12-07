#pragma once

#include <ren/renderer/graph/RenderPassTask.h>
#include <ren/renderer/graph/RunContext.h>
#include <ren/renderer/pipelines/PipelineStateObject.h>
#include <ren/renderer/graph/RenderGraph.h>
#include <ren/core/EngineUBO.h>
#include <ren/renderer/Buffer.h>

namespace ren {

  // This is a simple gbuffer render task for PBR rendering.
  class GBufferTask : public ren::RenderPassTask {
   public:
    struct {
      ren::GraphHandle albedo;             // albedo result
      ren::GraphHandle normal;             // normal result
      ren::GraphHandle metallicRoughness;  // metallic-roughness result
      ren::GraphHandle depth;              // depth result
    } out;



    // TODO: STORE THESE ON THE GRAPH RUN CONTEXT ALONG WITH CAMERA INFO AND SCENE INFO!
    ren::EngineUBO engineUBO;
    UniformBufferSet<ren::EngineUBO> engineUBOBuffer;

    GBufferTask(ren::RenderGraph &G);

    void run(ren::GraphRunContext &ctx) override;
    void inspect(void) override;
  };


  // TODO: figure out how to parameterize this with different cameras!
  inline auto &addGBuffer(RenderGraph &G, ren::GraphHandle &albedo, ren::GraphHandle &normal,
                          ren::GraphHandle &metallicRoughness, ren::GraphHandle &depth) {
    auto &pass = G.addTask<GBufferTask>("gbuffer");

    albedo = pass.out.albedo;
    normal = pass.out.normal;
    metallicRoughness = pass.out.metallicRoughness;
    depth = pass.out.depth;

    return pass;
  }

}  // namespace ren