#include "SSAOTask.h"
#include <ren/core/Application.h>
#include "ren/renderer/ShaderProgram.h"
#include <ren/renderer/graph/RenderGraph.h>

#include <ren/Camera.h>
#include <ren/renderer/Vulkan.h>

namespace ren {

  SSAOTask::SSAOTask(ren::RenderGraph &G, GraphHandle depth, GraphHandle normal)
      : ren::RenderPassTask(G) {
    this->in.depth = depth;
    this->in.normal = normal;

    auto scale = glm::vec2(1.0);
    // Not sure about the format right now.
    this->out.ssao = addColorAttachment("ssao", {.scale = scale, .format = VK_FORMAT_R8G8B8A8_SRGB});

    // pso.program = makeRef<ShaderProgram>("shaders/depth_pre_pass");
    // pso.depthWrite = true;
  }

  void SSAOTask::run(ren::GraphRunContext &ctx) {}
}  // namespace ren
