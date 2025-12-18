#include "GBufferTask.h"
#include <ren/Camera.h>
#include <ren/renderer/RenderWorld.h>
#include <ren/core/Application.h>
#include <ren/core/DebugLines.hpp>

namespace ren {

  GBufferTask::GBufferTask(ren::RenderGraph &G)
      : ren::RenderPassTask(G) {
    // Location 0
    this->out.albedo = addColorAttachment(
        "gbufferAlbedo", {.scale = glm::vec2(1.0f), .format = VK_FORMAT_R16G16B16A16_SFLOAT});
    // Location 1
    this->out.normal = addColorAttachment(
        "gbufferNormal", {.scale = glm::vec2(1.0f), .format = VK_FORMAT_R16G16B16A16_SFLOAT});
    // Location 2
    this->out.metallicRoughness = addColorAttachment(
        "gbufferMetallicRoughness", {.scale = glm::vec2(1.0f), .format = VK_FORMAT_R8G8B8A8_UNORM});
    this->out.depth = addDepthAttachment(
        "gbufferDepth", {.scale = glm::vec2(1.0f), .format = VK_FORMAT_D32_SFLOAT});
  }


  void GBufferTask::run(ren::GraphRunContext &ctx) {
    auto &cam = ren::Camera::get();
    auto viewMatrix = cam.view_matrix();

    // Grab an image for width/height.
    auto image = ctx.graph.getImage(out.depth);
    auto projection = ren::Camera::projectionMatrix(image->getWidth(), image->getHeight());


    auto &megaMesh = ren::world().get_mut<ren::MegaMeshBuffer>();
    // Bind the MegaMesh buffer for rendering geometry.
    megaMesh.bind(ctx.cmd);

    // TODO: BATCH RENDERING

    ren::MeshPushConstants pc;

    ren::RenderWorld rw(cam);
    rw.extractFromECS(ren::world());

    // for (auto &pl : rw.pointLights) {
    //   ren::println("Point Light at {},{},{} with radius {}", pl.position.x, pl.position.y,
    //                pl.position.z, pl.radius);
    //   DebugScribe s;
    //   s.drawSphere(pl.position, pl.radius, pl.color, 0.1f);
    //   s.drawSphere(pl.position, 0.1f, pl.color, 1.0f);
    //   // ren::debugLine(glm::vec3(0, 0, 0), pl.position, pl.color, 2.0f);
    // }


    pc.view = viewMatrix;
    pc.proj = projection;


    ren::emit<ren::DebugDrawEvent>({pc.view, pc.proj});  // TODO: REMOVE

    engineUBO.view = pc.view;
    engineUBO.proj = pc.proj;
    engineUBO.invViewProj = glm::inverse(pc.proj * pc.view);
    engineUBO.cameraWorldPosition = glm::vec4(cam.position, 1.0);

    engineUBO.time = ren::Application::get().timeSeconds;
    this->engineUBOBuffer.update(engineUBO);

    for (auto &r : rw.renderables) {
      auto &mesh = r.mesh;
      auto &mat = r.material;


      if (!mat->bind(ctx.renderer)) {
        continue;  // Skip this renderable if the material is not ready.
      }


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
    }
  }


  void GBufferTask::inspect(void) {
    graph().getResource(out.albedo)->inspect();
    graph().getResource(out.normal)->inspect();
    graph().getResource(out.metallicRoughness)->inspect();
    graph().getResource(out.depth)->inspect();
  }
}  // namespace ren