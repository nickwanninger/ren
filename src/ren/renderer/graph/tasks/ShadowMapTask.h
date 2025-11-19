#pragma once

#include <ren/renderer/graph/RenderPassTask.h>
#include <ren/renderer/graph/RunContext.h>
#include <ren/renderer/pipelines/PipelineStateObject.h>
#include <ren/renderer/graph/RenderGraph.h>

namespace ren {

  class ShadowMapTask : public ren::RenderPassTask {
   public:
    struct {
      ren::GraphHandle shadow;
    } out;


    ren::PipelineStateObject pso;

    glm::mat4 lightView, lightProj;


    glm::vec3 sunDirection = glm::vec3(0.0f, 1.0f, 1.0f);
    float shadowMapOrthoSize = 20.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;

    ShadowMapTask(ren::RenderGraph &G, u32 resolution = 1024);

    void run(ren::GraphRunContext &ctx) override;

    void inspect(void) override;
  };


  inline void addShadowMap(RenderGraph &G, u32 resolution, ren::GraphHandle &shadow) {
    auto &pass = G.addTask<ShadowMapTask>("shadow_map", resolution);
    shadow = pass.out.shadow;
  }

}  // namespace ren