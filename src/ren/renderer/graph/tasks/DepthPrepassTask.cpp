#include "DepthPrepassTask.h"
#include <ren/core/Application.h>
#include <ren/renderer/shader/ShaderCache.h>
#include <ren/renderer/graph/RenderGraph.h>
#include <ren/renderer/RenderWorld.h>

#include <ren/Camera.h>
#include <ren/renderer/graph/RunContext.h>


namespace ren {

  DepthPrepassTask::DepthPrepassTask(ren::RenderGraph &G)
      : ren::RenderPassTask(G) {
    auto scale = glm::vec2(1.0);
    this->depthOut = addDepthAttachment("dpp_depth", {.size = ImageSize::viewRelative(scale.x)});
    this->normalOut = addColorAttachment("dpp_normal", {.size = ImageSize::viewRelative(scale.x), .format = VK_FORMAT_R16G16B16A16_SFLOAT});

    pso.program = ren::getShader("shaders/depth_pre_pass");
    pso.depthWrite = true;
    pso.cullMode = ren::CullMode::Back;
  }

  void DepthPrepassTask::run(ren::GraphRenderPassContext &ctx) const {
  }
}  // namespace ren
