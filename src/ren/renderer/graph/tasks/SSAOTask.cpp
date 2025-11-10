#include "SSAOTask.h"
#include <ren/core/Application.h>
#include "ren/renderer/ShaderProgram.h"
#include <ren/renderer/graph/RenderGraph.h>

#include <ren/Camera.h>
#include <ren/renderer/Vulkan.h>
#include <imgui/imgui.h>
#include <random>


namespace ren {

  static ref<ren::Image> createSSAONoiseTexture(void) {
    const u32 noiseSize = 4;
    glm::vec4 noiseData[noiseSize * noiseSize];
    std::uniform_real_distribution<float> randomFloats(0.0,
                                                       1.0);  // random floats between [0.0, 1.0]
    std::default_random_engine generator;
    for (u32 i = 0; i < noiseSize * noiseSize; ++i) {
      glm::vec4 noise(randomFloats(generator) * 2.0 - 1.0, randomFloats(generator) * 2.0 - 1.0,
                      0.0f, 1.0f);
      noiseData[i] = glm::normalize(noise);
    }



    // make a staging buffer.
    VkDeviceSize bufferSize = sizeof(noiseData);
    ren::Buffer stagingBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              VMA_MEMORY_USAGE_CPU_ONLY);
    stagingBuffer.map();
    stagingBuffer.copyFromHost(noiseData, bufferSize, 0);
    stagingBuffer.unmap();


    ren::ImageBuilder b("ssao_noise_texture");
    VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
    b.setFormat(format);
    b.setWidth(noiseSize);
    b.setHeight(noiseSize);
    b.setUsage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    b.setAllocationUsage(VMA_MEMORY_USAGE_GPU_ONLY);


    auto image = b.build();

    // transfer data from staging buffer to image.
    auto &vulkan = ren::getVulkan();

    vulkan.transitionImageLayout(image->getImage(), format, VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    vulkan.copyBufferToImage(stagingBuffer.getHandle(), image->getImage(),
                             static_cast<uint32_t>(noiseSize), static_cast<uint32_t>(noiseSize));

    vulkan.transitionImageLayout(image->getImage(), format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    return image;
  }

  static float lerp(float a, float b, float f) { return a + f * (b - a); }

  SSAOTask::SSAOTask(ren::RenderGraph &G, float fscale, GraphHandle depth, GraphHandle normal)
      : ren::RenderPassTask(G) {
    this->in.depth = depth;
    this->in.normal = normal;



    auto scale = glm::vec2(fscale);
    // Not sure about the format right now.
    this->out.ssao =
        addColorAttachment("ssao", {.scale = scale, .format = VK_FORMAT_R8G8B8A8_UNORM});

    this->read(in.depth, ren::GraphAccess::ShaderRead);
    this->read(in.normal, ren::GraphAccess::ShaderRead);

    pso.program = ren::ShaderProgram::makeFullScreenProgram("shaders/ssao.frag");
    pso.cullMode = ren::CullMode::None;
    pso.depthTest = false;
    pso.depthWrite = false;
    pso.hasVertexBinding = false;

    ssao.radius = 0.06f;
    ssao.bias = 0.025f;
    ssao.num_samples = 12;

    this->noiseTexture = createSSAONoiseTexture();
  }

  void SSAOTask::run(ren::GraphRunContext &ctx) {
    ctx.renderer.bind(pso);


    auto &cam = ren::Camera::get();
    auto width = graph().getImage(out.ssao)->getWidth();
    auto height = graph().getImage(out.ssao)->getHeight();
    ssao.projection = ren::Camera::projectionMatrix(width, height);
    ssao.inv_projection = glm::inverse(ssao.projection);
    ssao.screen_size = glm::vec2(width, height);



    std::uniform_real_distribution<float> randomFloats(0.0,
                                                       1.0);  // random floats between [0.0, 1.0]
    std::default_random_engine generator;
    for (unsigned int i = 0; i < ssao.num_samples; ++i) {
      glm::vec4 sample(randomFloats(generator) * 2.0 - 1.0, randomFloats(generator) * 2.0 - 1.0,
                       randomFloats(generator) * 2.0 - 1.0, 0.0);

      sample = glm::normalize(sample);
      sample *= randomFloats(generator);

      float scale = float(i) / 64;
      scale = lerp(0.1f, 1.0f, scale * scale);
      sample *= scale;
      ssao.samples[i] = sample;
    }

    uSSAO.update(ssao);

    VkFilter filter = VK_FILTER_LINEAR;

    auto binder = ctx.renderer.startBinding(0);
    binder.bind("ssao", uSSAO);
    binder.bind("depth_sampler", *graph().getImage(in.depth), filter);
    binder.bind("normal_sampler", *graph().getImage(in.normal), filter);
    binder.bind("noise_sampler", *noiseTexture, filter);
    binder.apply();



    vkCmdDraw(ctx.cmd, 3, 1, 0, 0);
  }

  void SSAOTask::inspect(void) {
    pso.program->inspect();
    ImGui::Separator();
    ImGui::Text("SSAO Params:");
    ImGui::DragFloat("Radius", &ssao.radius, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat("Bias", &ssao.bias, 0.001f, 0.0f, 1.0f);
    ImGui::DragInt("Num Samples", &ssao.num_samples, 1, 1, 64);
  }




  /// Blur task
  SSAOBlurTask::SSAOBlurTask(ren::RenderGraph &G, float fscale, GraphHandle ssaoHandle)
      : ren::RenderPassTask(G) {
    this->in.ssao = ssaoHandle;


    auto scale = glm::vec2(fscale);
    this->out.ssao_blurred =
        addColorAttachment("ssao_blurred", {.scale = scale, .format = VK_FORMAT_R8G8B8A8_UNORM});

    this->read(in.ssao, ren::GraphAccess::ShaderRead);

    pso.program = ren::ShaderProgram::makeFullScreenProgram("shaders/ssao_blur.frag");
    pso.cullMode = ren::CullMode::None;
    pso.depthTest = false;
    pso.depthWrite = false;
    pso.hasVertexBinding = false;
  }


  void SSAOBlurTask::run(ren::GraphRunContext &ctx) {
    ctx.renderer.bind(pso);

    VkFilter filter = VK_FILTER_LINEAR;

    auto binder = ctx.renderer.startBinding(0);
    binder.bind("ssao", *graph().getImage(in.ssao), filter);
    binder.apply();

    vkCmdDraw(ctx.cmd, 3, 1, 0, 0);
  }




}  // namespace ren
