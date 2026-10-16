#include "SSAOTask.h"
#include <ren/core/Application.h>
#include <ren/renderer/shader/ShaderCache.h>
#include <ren/renderer/graph/RenderGraph.h>

#include <ren/renderer/vulkan/Vulkan.h>
#include <imgui/imgui.h>
#include "ren/renderer/graph/RunContext.h"

#include <ren/renderer/Texture.h>
#include <ren/renderer/graph/ImageResource.h>

namespace ren {

  constexpr VkFormat ssaoFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

  // The noise is data, not color, so it is UNORM. Its sampler repeats, which
  // the shader needs to tile it over the screen.
  static ref<ren::Texture> loadSSAONoiseTexture(void) {
    std::vector<u8> noiseBytes;
    if (!ren::getAssetManager().load("shaders/noise/bluenoise64.png", noiseBytes)) {
      throw std::runtime_error("Failed to load SSAO noise texture");
    }
    auto texture = ren::Texture::load("ssao_noise", noiseBytes.data(), noiseBytes.size(), VK_FORMAT_R8G8B8A8_UNORM);
    if (!texture) {
      throw std::runtime_error("Failed to decode SSAO noise texture");
    }
    return texture;
  }

  SSAOTask::SSAOTask(ren::RenderGraph &G, float fscale, GraphHandle depth, GraphHandle normal)
      : ren::RenderPassTask(G) {
    this->in.depth = depth;
    this->in.normal = normal;

    auto scale = glm::vec2(fscale);
    this->out.ssao = addColorAttachment("ssao", {.relativeScale = scale, .format = ssaoFormat});

    this->read(in.depth, ren::GraphAccess::ShaderRead);
    this->read(in.normal, ren::GraphAccess::ShaderRead);

    pso.debugName = "SSAO";
    pso.program = ren::getShader("ren/core/ssao", "ren/pass/fullscreen");
    // The fullscreen triangle comes from SV_VertexID: no vertex buffer, no
    // culling, and no depth.
    pso.hasVertexBinding = false;
    pso.cullMode = ren::CullMode::None;
    pso.depthTest = false;
    pso.depthWrite = false;

    this->noiseTexture = loadSSAONoiseTexture();
  }

  void SSAOTask::run(ren::GraphRenderPassContext &ctx) {
    // One noise texel per output pixel. The output is smaller than the
    // G-buffer when the task scale is below 1.
    auto target = graph().getImage(out.ssao);
    glm::vec2 noiseScale = glm::vec2(target->getWidth(), target->getHeight()) /
                           glm::vec2(noiseTexture->getWidth(), noiseTexture->getHeight());

    auto shader = ctx.encoder.bindGraphics(pso);
    auto root = shader.root("root");
    root.set("depth", graph().get<ren::ImageResource>(in.depth)->textureHandle());
    root.set("normal", graph().get<ren::ImageResource>(in.normal)->textureHandle());
    root.set("noise", noiseTexture->handle());
    root.set("noiseScale", noiseScale);
    root.set("radius", radius);
    root.set("bias", bias);
    root.set("sampleCount", static_cast<u32>(sampleCount));
    shader.drawFullscreenTriangle();
  }

  void SSAOTask::inspect(void) {
    ImGui::DragFloat("Radius", &radius, 0.01f, 0.01f, 10.0f);
    ImGui::DragFloat("Bias", &bias, 0.001f, 0.0f, 1.0f);
    ImGui::SliderInt("Samples", &sampleCount, 1, 64);
    ImGui::Separator();
    graph().getResource(out.ssao)->inspect();
  }




  /// Blur task
  SSAOBlurTask::SSAOBlurTask(ren::RenderGraph &G, float fscale, GraphHandle ssaoHandle, GraphHandle depthHandle, GraphHandle normalHandle)
      : ren::RenderPassTask(G) {
    this->in.ssao = ssaoHandle;
    this->in.depth = depthHandle;
    this->in.normal = normalHandle;

    auto scale = glm::vec2(fscale);
    this->out.ssao_blurred = addColorAttachment("ssao_blurred", {.relativeScale = scale, .format = ssaoFormat});

    this->read(in.ssao, ren::GraphAccess::ShaderRead);
    this->read(in.depth, ren::GraphAccess::ShaderRead);
    this->read(in.normal, ren::GraphAccess::ShaderRead);

    pso.debugName = "SSAOBlur";
    pso.program = ren::getShader("ren/core/ssao_blur", "ren/pass/fullscreen");
    pso.hasVertexBinding = false;
    pso.cullMode = ren::CullMode::None;
    pso.depthTest = false;
    pso.depthWrite = false;
  }


  void SSAOBlurTask::run(ren::GraphRenderPassContext &ctx) {
    // The taps step in SSAO texels, which are larger than output texels when
    // the SSAO runs at a lower scale.
    auto ssaoImage = graph().getImage(in.ssao);
    glm::vec2 ssaoTexelSize = 1.0f / glm::vec2(ssaoImage->getWidth(), ssaoImage->getHeight());

    auto shader = ctx.encoder.bindGraphics(pso);
    auto root = shader.root("root");
    root.set("ssao", graph().get<ren::ImageResource>(in.ssao)->textureHandle());
    root.set("depth", graph().get<ren::ImageResource>(in.depth)->textureHandle());
    root.set("normal", graph().get<ren::ImageResource>(in.normal)->textureHandle());
    root.set("ssaoTexelSize", ssaoTexelSize);
    shader.drawFullscreenTriangle();
  }

  void SSAOBlurTask::inspect(void) { graph().getResource(out.ssao_blurred)->inspect(); }

}  // namespace ren
