#include "SSAOTask.h"
#include <ren/core/Application.h>
#include <ren/renderer/shader/ShaderProgram.h>
#include <ren/renderer/graph/RenderGraph.h>

#include <ren/Camera.h>
#include <ren/renderer/vulkan/Vulkan.h>
#include <imgui/imgui.h>
#include <random>

#include <stb/stb_image.h>

namespace ren {

  constexpr VkFormat ssaoFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

  static const u32 ssaoNoiseSize = 8;
  static ref<ren::Image> createSSAONoiseTexture(void) {
    auto &am = ren::getAssetManager();
    // TODO: load textures correctly!
    std::vector<u8> noiseBytes;
    if (am.load("shaders/noise/bluenoise64.png", noiseBytes) == false) {
      throw std::runtime_error("Failed to load SSAO noise texture");
    }



    int texWidth, texHeight, texChannels;
    stbi_uc *pixels = nullptr;


    pixels = stbi_load_from_memory((stbi_uc *)noiseBytes.data(), (int)noiseBytes.size(), &texWidth,
                                   &texHeight, &texChannels, STBI_rgb_alpha);


    std::vector<glm::vec4> noiseData(texWidth * texHeight);
    for (u32 y = 0; y < (u32)texHeight; ++y) {
      for (u32 x = 0; x < (u32)texWidth; ++x) {
        u32 index = y * texWidth + x;
        u8 r = pixels[index * 4 + 0];
        u8 g = pixels[index * 4 + 1];
        u8 b = pixels[index * 4 + 2];
        // Map from [0,255] to [-1,1]
        glm::vec4 noise((r / 255.0f) * 2.0f - 1.0f, (g / 255.0f) * 2.0f - 1.0f,
                        (b / 255.0f) * 2.0f - 1.0f, 1.0f);
        noiseData[index] = noise;
      }
    }


    stbi_image_free(pixels);


    // make a staging buffer.
    VkDeviceSize bufferSize = noiseData.size() * sizeof(glm::vec4);
    ren::Buffer stagingBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              VMA_MEMORY_USAGE_CPU_ONLY);
    stagingBuffer.map();
    stagingBuffer.copyFromHost(noiseData.data(), bufferSize, 0);
    stagingBuffer.unmap();


    ren::ImageBuilder b("ssao_noise_texture");
    VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
    b.setFormat(format);
    b.setWidth(texWidth);
    b.setHeight(texHeight);
    b.setUsage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    b.setAllocationUsage(VMA_MEMORY_USAGE_GPU_ONLY);


    auto image = b.build();

    // transfer data from staging buffer to image.
    auto &vulkan = ren::getVulkan();

    vulkan.transitionImageLayout(image->getImage(), format, VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    vulkan.copyBufferToImage(stagingBuffer.getHandle(), image->getImage(),
                             static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));

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
    this->out.ssao = addColorAttachment("ssao", {.scale = scale, .format = ssaoFormat});

    this->read(in.depth, ren::GraphAccess::ShaderRead);
    this->read(in.normal, ren::GraphAccess::ShaderRead);

    pso.program = ren::ShaderProgram::makeFullScreenProgram("shaders/ssao.frag");
    pso.cullMode = ren::CullMode::None;
    pso.depthTest = false;
    pso.depthWrite = false;
    pso.hasVertexBinding = false;



    this->noiseTexture = createSSAONoiseTexture();
    ssao.noise_divide = 64;  // this->noiseTexture->getWidth();
  }

  void SSAOTask::run(ren::GraphRunContext &ctx) {
    ctx.renderer.bind(pso);



    auto &cam = ren::Camera::get();
    auto viewMatrix = cam.view_matrix();

    auto ssaoImage = graph().getImage(out.ssao);
    auto width = ssaoImage->getWidth();
    auto height = ssaoImage->getHeight();

    ssao.normal_matrix = glm::transpose(glm::inverse(viewMatrix));
    ssao.projection = ren::Camera::projectionMatrix(width, height);
    ssao.inv_projection = glm::inverse(ssao.projection);
    ssao.screen_size = glm::vec2(width, height);
    std::uniform_real_distribution<float> randomFloats(0.0,
                                                       1.0);  // random floats between [0.0, 1.0]
    std::default_random_engine generator;
    for (unsigned int i = 0; i < ssao.num_samples; ++i) {
      // Generate a random sample vector in tangent space pointing up the hemisphere
      glm::vec4 sample(randomFloats(generator) * 2.0 - 1.0, randomFloats(generator) * 2.0 - 1.0,
                       randomFloats(generator), 0.0);

      sample = glm::normalize(sample);
      sample *= randomFloats(generator);

      float scale = float(i) / (float)ssao.num_samples;
      scale = lerp(0.1f, 1.0f, scale * scale);
      sample *= scale;

      ssao.samples[i] = sample;
    }



    uSSAO.update(ssao);

    VkFilter filter = VK_FILTER_NEAREST;

    auto binder = ctx.renderer.startBinding(0);
    binder.bind("ssao", uSSAO);
    binder.bind("depth_sampler", *graph().getImage(in.depth), filter);
    binder.bind("normal_sampler", *graph().getImage(in.normal), filter);
    binder.bind("noise_sampler", *noiseTexture, VK_FILTER_NEAREST);
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
    ImGui::DragFloat("Noise Divide", &ssao.noise_divide, 0.1f, 1.0f, 20.0f);
    ImGui::DragFloat("Intensity", &ssao.intensity, 0.1f, 0.1f, 10.0f);

    graph().getResource(out.ssao)->inspect();
  }




  /// Blur task
  SSAOBlurTask::SSAOBlurTask(ren::RenderGraph &G, float fscale, GraphHandle ssaoHandle,
                             GraphHandle depthHandle, GraphHandle normalHandle)
      : ren::RenderPassTask(G) {
    this->in.ssao = ssaoHandle;
    this->in.depth = depthHandle;
    this->in.normal = normalHandle;

    auto scale = glm::vec2(fscale);
    this->out.ssao_blurred =
        addColorAttachment("ssao_blurred", {.scale = scale, .format = ssaoFormat});

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
    binder.bind("depth", *graph().getImage(in.depth), VK_FILTER_NEAREST);
    binder.bind("normal", *graph().getImage(in.normal), VK_FILTER_NEAREST);
    binder.apply();

    vkCmdDraw(ctx.cmd, 3, 1, 0, 0);
  }




}  // namespace ren
