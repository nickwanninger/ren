#include "BloomTask.h"
#include <ren/renderer/graph/ImageResource.h>
#include <ren/renderer/shader/ShaderCache.h>
#include <ren/renderer/Renderer.h>
#include <imgui/imgui.h>
#include <array>

namespace ren {

  namespace {
    void useLinearClamp(RenderGraph &graph, GraphHandle image) {
      SamplerDesc desc;
      desc.minFilter = desc.magFilter = VK_FILTER_LINEAR;
      desc.addressModeU = desc.addressModeV = desc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      graph.get<ImageResource>(image)->sampler = Renderer::get().getSamplerCache().get(desc);
    }

    void setupFullscreen(PipelineStateObject &pso, const char *name, const char *program) {
      pso.debugName = name;
      pso.program = getShader(program, "ren/pass/fullscreen");
      pso.hasVertexBinding = false;
      pso.cullMode = CullMode::None;
      pso.depthTest = false;
      pso.depthWrite = false;
    }
  }  // namespace

  BloomFilterTask::BloomFilterTask(RenderGraph &graph, float scale, GraphHandle source, GraphHandle detail, bool upsample, bool prefilter)
      : RenderPassTask(graph)
      , source(source)
      , detail(detail)
      , upsample(upsample)
      , prefilter(prefilter) {
    output = addColorAttachment("bloom", {.relativeScale = glm::vec2(scale), .format = VK_FORMAT_R16G16B16A16_SFLOAT});
    read(source, GraphAccess::ShaderRead);
    useLinearClamp(graph, source);
    if (upsample) {
      read(detail, GraphAccess::ShaderRead);
      useLinearClamp(graph, detail);
    }
    setupFullscreen(pso, "BloomFilter", "ren/core/bloom_filter");
  }

  void BloomFilterTask::run(GraphRenderPassContext &ctx) {
    auto image = graph().getImage(source);
    auto shader = ctx.encoder.bindGraphics(pso);
    auto root = shader.root("root");
    root.set("source", graph().get<ImageResource>(source)->textureHandle());
    // Bind a valid texture even when the downsample branch does not use it.
    auto detailHandle = upsample ? detail : source;
    auto detailImage = graph().getImage(detailHandle);
    root.set("detail", graph().get<ImageResource>(detailHandle)->textureHandle());
    root.set("sourceTexelSize", 1.0f / glm::vec2(image->getWidth(), image->getHeight()));
    root.set("detailTexelSize", 1.0f / glm::vec2(detailImage->getWidth(), detailImage->getHeight()));
    root.set("upsample", static_cast<u32>(upsample));
    root.set("prefilter", static_cast<u32>(prefilter));
    root.set("threshold", threshold);
    shader.drawFullscreenTriangle();
  }

  void BloomFilterTask::inspect() { graph().getResource(output)->inspect(); }

  BloomCompositeTask::BloomCompositeTask(RenderGraph &graph, GraphHandle scene, GraphHandle bloom)
      : RenderPassTask(graph)
      , scene(scene)
      , bloom(bloom) {
    output = addColorAttachment("tonemapped", {.relativeScale = glm::vec2(1.0f), .format = VK_FORMAT_R8G8B8A8_SRGB});
    read(scene, GraphAccess::ShaderRead);
    read(bloom, GraphAccess::ShaderRead);
    useLinearClamp(graph, bloom);
    setupFullscreen(pso, "BloomComposite", "ren/core/bloom_composite");
  }

  void BloomCompositeTask::run(GraphRenderPassContext &ctx) {
    auto image = graph().getImage(bloom);
    auto shader = ctx.encoder.bindGraphics(pso);
    auto root = shader.root("root");
    root.set("scene", graph().get<ImageResource>(scene)->textureHandle());
    root.set("bloom", graph().get<ImageResource>(bloom)->textureHandle());
    root.set("bloomTexelSize", 1.0f / glm::vec2(image->getWidth(), image->getHeight()));
    root.set("strength", strength);
    root.set("exposure", exposure);
    root.set("bypass", static_cast<u32>(bypass));
    shader.drawFullscreenTriangle();
  }

  void BloomCompositeTask::inspect() {
    ImGui::SliderFloat("Bloom strength", &strength, 0.0f, 1.0f, "%.3f");
    if (prefilter) {
      ImGui::SliderFloat("Bloom threshold", &prefilter->threshold, 0.0f, 10.0f, "%.2f");
    }
  }

  BloomCompositeTask &addBloom(RenderGraph &graph, GraphHandle scene, GraphHandle &output) {
    constexpr size_t levels = 5;
    std::array<GraphHandle, levels> down;
    std::array<float, levels> scales;
    GraphHandle source = scene;
    BloomFilterTask *prefilter = nullptr;
    float scale = 0.5f;
    for (size_t i = 0; i < levels; ++i) {
      auto name = "bloom_down_" + std::to_string(i);
      auto &task = graph.addTask<BloomFilterTask>(name.c_str(), scale, source, nullGraphHandle, false, i == 0);
      if (i == 0) {
        prefilter = &task;
      }
      down[i] = source = task.output;
      scales[i] = scale;
      scale *= 0.5f;
    }
    for (int i = static_cast<int>(levels) - 2; i >= 0; --i) {
      auto name = "bloom_up_" + std::to_string(i);
      auto &task = graph.addTask<BloomFilterTask>(name.c_str(), scales[i], source, down[i], true, false);
      source = task.output;
    }
    auto &composite = graph.addTask<BloomCompositeTask>("bloom_composite", scene, source);
    composite.prefilter = prefilter;
    output = composite.output;
    return composite;
  }

}  // namespace ren
