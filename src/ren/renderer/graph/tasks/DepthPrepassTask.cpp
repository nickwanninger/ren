#include "DepthPrepassTask.h"
#include <ren/core/Application.h>
#include <ren/renderer/shader/ShaderProgram.h>
#include <ren/renderer/graph/RenderGraph.h>
#include <ren/renderer/RenderWorld.h>

#include <ren/Camera.h>

namespace ren {

  DepthPrepassTask::DepthPrepassTask(ren::RenderGraph &G)
      : ren::RenderPassTask(G) {
    auto scale = glm::vec2(1.0);
    this->depthOut = addDepthAttachment("dpp_depth", {.scale = scale});
    this->normalOut =
        addColorAttachment("dpp_normal", {.scale = scale, .format = VK_FORMAT_R16G16B16A16_SFLOAT});

    pso.program = make<ShaderProgram>("shaders/depth_pre_pass");
    pso.depthWrite = true;
    pso.cullMode = ren::CullMode::Back;
  }

  void DepthPrepassTask::run(ren::GraphRunContext &ctx) {
    auto &camera = ren::Camera::get();

    auto &megaMesh = ren::world().get_mut<ren::MegaMeshBuffer>();
    megaMesh.bind(ctx.cmd);

    auto image = ctx.graph.getImage(depthOut);
    auto projection = ren::Camera::projectionMatrix(image->getWidth(), image->getHeight());

    ren::MeshPushConstants pc;

    ren::RenderWorld rw(camera);
    rw.extractFromECS(ren::world());


    pc.view = camera.view_matrix();
    pc.proj = projection;
    ctx.renderer.bind(pso);

    for (auto &r : rw.renderables) {
      auto &entry = megaMesh.getEntry(r.mesh->megaHandle);
      pc.model = r.transform;
      pc.normalMatrix = glm::transpose(glm::inverse(pc.model));

      ctx.renderer.setPushConstants(pc);

      vkCmdDrawIndexed(ctx.cmd, entry.indexCount, 1, entry.indexOffset, entry.vertexOffset, 0);
    }
  }
}  // namespace ren
