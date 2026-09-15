#include "GBufferTask.h"
#include <ren/Camera.h>
#include <ren/renderer/RenderWorld.h>
#include <ren/core/Application.h>
#include <ren/core/DebugLines.hpp>
#include "ren/renderer/graph/RunContext.h"

namespace ren {

  GBufferTask::GBufferTask(ren::RenderGraph &G)
      : ren::RenderPassTask(G) {
    auto scale = glm::vec2(1.0f);
    // Location 0
    this->out.albedo = addColorAttachment("gbufferAlbedo", {.relativeScale = scale, .format = VK_FORMAT_R16G16B16A16_SFLOAT});
    // Location 1
    this->out.normal = addColorAttachment("gbufferNormal", {.relativeScale = scale, .format = VK_FORMAT_R16G16B16A16_SFLOAT});
    // Location 2
    this->out.metallicRoughness = addColorAttachment("gbufferMetallicRoughness", {.relativeScale = scale, .format = VK_FORMAT_R8G8B8A8_UNORM});
    this->out.depth = addDepthAttachment("gbufferDepth", {.relativeScale = scale, .format = VK_FORMAT_D32_SFLOAT});
  }


  void GBufferTask::run(ren::GraphRenderPassContext &ctx) {
    auto &cam = ren::Camera::get();
    auto viewMatrix = cam.view_matrix();

    // return;

    // Grab an image for width/height.
    auto image = ctx.graph.getImage(out.depth);
    auto projection = ren::Camera::projectionMatrix(image->getWidth(), image->getHeight());


    auto &megaMesh = ren::world().get_mut<ren::MegaMeshBuffer>();
    // Bind the MegaMesh buffer for rendering geometry.
    megaMesh.bind(ctx.encoder.getEncoder());

    // TODO: BATCH RENDERING

    ren::MeshPushConstants pc;

    ren::RenderWorld rw(cam);
    rw.extractFromECS(ren::world());

    pc.view = viewMatrix;
    pc.proj = projection;


    ren::emit<ren::DebugDrawEvent>({pc.view, pc.proj});  // TODO: REMOVE

    engineUBO.view = pc.view;
    engineUBO.proj = pc.proj;
    engineUBO.invViewProj = glm::inverse(pc.proj * pc.view);
    engineUBO.cameraWorldPosition = glm::vec4(cam.position, 1.0);

    engineUBO.time = ren::Application::get().timeSeconds;
    this->engineUBOBuffer.update(engineUBO);

    float radius = 0.01f;
    for (auto &r : rw.renderables) {
      auto &mesh = r.mesh;
      auto &mat = r.material;


      // auto x = ctx.encoder.bindGraphics(mat->getPSO());

      //  auto cur = ctx.encoder.bindGraphics(mat->getPSO());


      DebugScribe s;
      auto position = r.transform * glm::vec4(0, 0, 0, 1);
      s.drawSphere(glm::vec3(position), radius, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f), 1.0f);
      radius += 0.05f;


      // if (!mat->bind(ctx.renderer)) {
      //   continue;  // Skip this renderable if the material is not ready.
      // }

      /*

      auto engineBinder = ctx.renderer.startBinding(0);
      engineBinder.bind("engine", this->engineUBOBuffer);
      engineBinder.apply();


      auto &meshEntry = megaMesh.getEntry(r.mesh->megaHandle);
      pc.model = r.transform;
      pc.normalMatrix = glm::transpose(glm::inverse(pc.model));

      ctx.renderer.setPushConstants(pc);

      int instanceCount = 1;
      vkCmdDrawIndexed(ctx.cmd, meshEntry.indexCount, instanceCount, meshEntry.indexOffset,
                       meshEntry.vertexOffset, 0);
                       */
    }
  }


  void GBufferTask::inspect(void) {
    graph().getResource(out.albedo)->inspect();
    graph().getResource(out.normal)->inspect();
    graph().getResource(out.metallicRoughness)->inspect();
    graph().getResource(out.depth)->inspect();
  }
}  // namespace ren