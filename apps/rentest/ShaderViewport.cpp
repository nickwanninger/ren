#include "ShaderViewport.h"

#include <ren/renderer/CommandEncoder.h>
#include <ren/renderer/graph/RenderPassTask.h>
#include <ren/renderer/shader/ShaderCache.h>

namespace rentest {


  void ShaderViewport::buildGraph() {
    pso.debugName = shader;
    pso.program = ren::getShader(shader, "ren/pass/fullscreen");

    // The fullscreen triangle comes from SV_VertexID, so there is no vertex
    // buffer, nothing to cull, and no depth.
    pso.hasVertexBinding = false;
    pso.cullMode = ren::CullMode::None;
    pso.depthTest = false;
    pso.depthWrite = false;

    graph.pass(shader.c_str())
        .createColorAttachment("shaderColor", {.size = ren::ImageSize::viewRelative(1.0f), .format = format}, finalImage)
        .render([this](ren::GraphRenderPassContext &ctx) {
          auto bound = ctx.encoder.bindGraphics(pso);
          bound.drawFullscreenTriangle();
        });
  }

}  // namespace rentest
