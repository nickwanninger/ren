#include "DepthPrepassTask.h"
#include <ren/core/Application.h>
#include "ren/renderer/ShaderProgram.h"
#include <ren/renderer/graph/RenderGraph.h>

#include <ren/Camera.h>

namespace ren {

  DepthPrepassTask::DepthPrepassTask(ren::RenderGraph &G)
      : ren::RenderPassTask(G) {
    auto scale = glm::vec2(0.125);
    this->depthOut = addDepthAttachment("dpp_depth", {.scale = scale});
    this->normalOut = addColorAttachment("dpp_normal", {.scale = scale});

    pso.program = makeRef<ShaderProgram>("shaders/depth_pre_pass");
    pso.depthWrite = true;
  }

  void DepthPrepassTask::run(ren::GraphRunContext &ctx) {
    std::unordered_set<ren::Mesh *> uniqueMeshes;

    std::vector<std::pair<ren::Mesh *, glm::mat4>> toDraw;


    ren::world()
        .query<comp::Mesh, comp::Transform, comp::Material>("ren::core::renderer::BatchBuildQuery")
        .each([&](const comp::Mesh &mesh, const comp::Transform &transform,
                  const comp::Material &material) {
          uniqueMeshes.insert(mesh.mesh.get());
          // Create a batch for each mesh instance.
          // batch.mesh = mesh.mesh;
          // batch.transform = transform.transformMatrix;
          // batch.material = material.material;

          toDraw.push_back({mesh.mesh.get(), transform.transformMatrix});
        });

    auto &camera = ren::Camera::get();

    auto &megaMesh = ren::world().get_mut<ren::MegaMeshBuffer>();
    megaMesh.bind(ctx.cmd);

    auto image = ctx.graph.getImage(depthOut);
    auto projection = ren::Camera::projectionMatrix(image->getWidth(), image->getHeight());

    ren::MeshPushConstants pc;


    pc.view = camera.view_matrix();
    pc.proj = projection;
    ctx.renderer.bind(pso);
    for (auto &[mesh, position] : toDraw) {
      auto &entry = megaMesh.getEntry(mesh->megaHandle);
      pc.model = position;
      pc.normalMatrix = glm::transpose(glm::inverse(pc.model));

      ctx.renderer.setPushConstants(pc);

      vkCmdDrawIndexed(ctx.cmd, entry.indexCount, 1, entry.indexOffset, entry.vertexOffset, 0);
    }
  }
}  // namespace ren
