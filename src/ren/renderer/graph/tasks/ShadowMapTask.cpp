#include "ShadowMapTask.h"
#include <ren/core/Application.h>
#include "ren/renderer/ShaderProgram.h"
#include <ren/renderer/graph/RenderGraph.h>
#include <ren/renderer/RenderWorld.h>

#include <ren/core/DebugLines.hpp>

#include <imgui/imgui.h>
#include <ren/Camera.h>
#include <array>
#include <limits>

namespace ren {




  ShadowMapTask::ShadowMapTask(ren::RenderGraph &G, u32 resolution)
      : ren::RenderPassTask(G) {
    out.shadow = addDepthAttachment("shadow_map", {.width = resolution, .height = resolution});

    pso.program = makeRef<ShaderProgram>("shaders/shadow_map");
    pso.depthWrite = true;
    pso.depthTest = true;
    pso.cullMode = ren::CullMode::Front;
  }

  void ShadowMapTask::run(ren::GraphRunContext &ctx) {
    auto &viewCamera = ren::Camera::get();

    auto &megaMesh = ren::world().get_mut<ren::MegaMeshBuffer>();
    megaMesh.bind(ctx.cmd);

    auto image = ctx.graph.getImage(out.shadow);

    ren::RenderWorld rw(viewCamera);
    rw.extractFromECS(ren::world());


    // sunDirection = glm::normalize(viewCamera.position + glm::vec3(0, 5, 0));
    float t = ren::Application::get().timeSeconds * 0.1;
    t = 1.0f;
    sunDirection = glm::normalize(glm::vec3(sin(t), 1.0f, cos(t)));


    glm::vec3 center = glm::vec3(0.0f);
    center = glm::round(viewCamera.position);


    float shadowMapDepth = farPlane - nearPlane;

    glm::vec3 sunPos = center + (glm::normalize(sunDirection) * (shadowMapDepth / 2.0f));

    ren::debugLine(center, sunPos, glm::vec3(1.0f, 1.0f, 0.0f), 0.1f);
    ren::debugLine(center, glm::vec3(sunPos.x, 0, sunPos.z), glm::vec3(1, 0, 0), 0.1f);
    ren::debugLine(sunPos, glm::vec3(sunPos.x, 0, sunPos.z), glm::vec3(0, 1, 0), 0.1f);

    this->lightView = glm::lookAt(sunPos, center, glm::vec3(0.0f, 1.0f, 0.0f));
    this->lightProj = glm::ortho(-shadowMapOrthoSize, shadowMapOrthoSize, -shadowMapOrthoSize,
                                     shadowMapOrthoSize, nearPlane, farPlane);

    lightProj[1][1] *= -1;


    ren::MeshPushConstants pc;
    pc.view = lightView;
    pc.proj = lightProj;
    ctx.renderer.bind(pso);

    for (auto &r : rw.renderables) {
      auto &entry = megaMesh.getEntry(r.mesh->megaHandle);
      pc.model = r.transform;
      pc.normalMatrix = glm::transpose(glm::inverse(pc.model));

      ctx.renderer.setPushConstants(pc);

      vkCmdDrawIndexed(ctx.cmd, entry.indexCount, 1, entry.indexOffset, entry.vertexOffset, 0);
    }
  }

  void ShadowMapTask::inspect(void) {
    ImGui::SliderFloat("Shadow Map Ortho Size", &shadowMapOrthoSize, 1.0f, 100.0f);
    ImGui::SliderFloat("Near Plane", &nearPlane, 0.01f, 10.0f);
    ImGui::SliderFloat("Far Plane", &farPlane, 10.0f, 100.0f);
    if (ImGui::SliderFloat3("Sun Direction", &sunDirection.x, -1.0f, 1.0f)) {
      sunDirection = glm::normalize(sunDirection);
    };

    graph().getResource(out.shadow)->inspect();
  }
}  // namespace ren
