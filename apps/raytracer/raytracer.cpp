
#include <ren/core/Instrumentation.h>
#include <ren/core/Application.h>
#include <ren/renderer/graph/RenderGraph.h>
#include <ren/renderer/shader/ShaderReflection.h>

#include <ren/core/Flag.h>
#include <ren/assets/MeshScene.hpp>
#include "ren/renderer/RenderWorld.h"
#include <ren/core/ThreadPool.h>

#include <ren/core/Systems.h>
#include <ren/core/Components.h>
#include <ren/renderer/submission/SubmissionUnit.h>
#include <ren/core/Result.h>
#include <ren/renderer/Viewport.h>



using namespace ren;


static ren::Flag<std::string> skyBoxPath("skybox", "assets/hdri/sky.jpg", "Path to the skybox texture to use in the viewport.");
static ren::Flag<std::string> scenePath("scene", "assets/test/meshes/simple_scene.glb", "Path to the scene mesh");


namespace shd {

  // These are shader structures we need to be careful with it.
  struct RayTracingMaterial {
    glm::vec4 color;
    glm::vec4 emissionColor = glm::vec4(0.0f);
    float smoothness = 0.0f;
  };

  struct Sphere {
    glm::vec3 position;
    float radius;
    RayTracingMaterial material;
  };


  // The padding makes the layout the same under std430, scalar and natural
  // layout rules.
  struct Mesh {
    // Moves a world-space ray into the model space of the mesh.
    glm::mat4 worldToModel;
    // Bounding box in model space.
    glm::vec3 minBounds;
    // The triangles of the mesh in the MegaMeshBuffer index buffer.
    u32 indexOffset;
    glm::vec3 maxBounds;
    u32 indexCount;
    // The indices are relative to this vertex in the MegaMeshBuffer.
    u32 vertexOffset;
    u32 _pad[3];
  };
  static_assert(sizeof(Mesh) == 112);
  static_assert(offsetof(Mesh, minBounds) == 64);
  static_assert(offsetof(Mesh, indexOffset) == 76);
  static_assert(offsetof(Mesh, maxBounds) == 80);
  static_assert(offsetof(Mesh, indexCount) == 92);
  static_assert(offsetof(Mesh, vertexOffset) == 96);

}  // namespace shd

class RaytraceViewport : public ren::EditorViewport {
 public:
  std::vector<shd::Sphere> spheres;
  PipelineStateObject rtPSO;
  u32 maxBounces = 4;
  u32 raysPerPixel = 1;
  bool randomizeSeed = true;
  u32 randomSeed = 0;

  // Progressive accumulation. Each pixel keeps the running average of every
  // frame since the last reset, in a buffer the graph does not own.
  bool accumulate = true;
  u32 accumulatedFrames = 0;
  ren::Box<StorageBuffer<glm::vec4>> accumulation;

  // What the current average was rendered with. Any change resets it.
  glm::mat4 lastView = glm::mat4(0.0f);
  glm::mat4 lastProjection = glm::mat4(0.0f);
  glm::uvec2 lastSize = {0, 0};
  u32 lastMaxBounces = 0;

  ref<MeshScene> scene;


  ref<Texture> skybox;

  // A camera that moves less than this per matrix element counts as still.
  static constexpr float kStillEpsilon = 1e-4f;

  static bool nearlyEqual(const glm::mat4 &a, const glm::mat4 &b) {
    for (int c = 0; c < 4; c++) {
      for (int r = 0; r < 4; r++) {
        if (glm::abs(a[c][r] - b[c][r]) > kStillEpsilon) {
          return false;
        }
      }
    }
    return true;
  }


  void buildGraph() override {
    scene = ren::MeshScene::load(scenePath.get());
    scene->instantiate({});
    spheres.push_back(
        {{8.600000, 10.400000, 0.000000}, 2.800000, {{1.000000, 0.546569, 0.093137, 1.000000}, {1.000000, 0.000000, 0.000000, 10.000000}}});
    spheres.push_back(
        {{-6.600000, 8.500000, 4.500000}, 2.400000, {{1.000000, 1.000000, 1.000000, 1.000000}, {0.000000, 0.264706, 1.000000, 10.000000}}});
    spheres.push_back(
        {{-2.700000, 10.200000, -6.700000}, 2.200000, {{1.000000, 1.000000, 1.000000, 1.000000}, {0.088235, 1.000000, 0.000000, 7.903000}}});


    // spheres.push_back(
    //     {{1.600000, 3.100000, 0.000000}, 1.200000, {{1.000000, 1.000000, 0.100000, 1.000000}, {0.000000, 0.000000, 0.000000, 0.000000}}});
    // spheres.push_back(
    //     {{0.000000, 2.800000, 1.500000}, 0.800000, {{0.100000, 0.200000, 1.000000, 1.000000}, {0.000000, 0.000000, 0.000000, 0.000000}}});
    // spheres.push_back(
    //     {{0.000000, -7.900000, 0.000000}, 10.000000, {{1.000000, 1.000000, 1.000000, 1.000000}, {0.000000, 0.000000, 0.000000, 0.000000}}});
    // spheres.push_back(
    //     {{-0.600000, 2.800000, -1.000000}, 1.000000, {{0.897059, 0.140715, 0.140715, 1.000000}, {0.000000, 0.000000, 0.000000, 0.000000}}});


    this->renderScale = 0.5f;


    this->skybox = ren::Texture::load(skyBoxPath.get(), VK_FORMAT_R8G8B8A8_SRGB);

    rtPSO.debugName = "Raytracer";
    rtPSO.program = ren::getShader("raytrace", "ren/pass/fullscreen");
    rtPSO.hasVertexBinding = false;
    rtPSO.cullMode = ren::CullMode::None;
    rtPSO.depthTest = false;
    rtPSO.depthWrite = false;

    graph.pass("raytrace")
        .createColorAttachment("output", {.relativeScale = glm::vec2(1.0f), .format = VK_FORMAT_R8G8B8A8_SRGB}, finalImage)
        .render([this](GraphRenderPassContext &ctx) { this->runRaytracer(ctx.encoder); });
  }

  void runRaytracer(ren::RenderPassEncoder &encoder) {
    auto shader = encoder.bindGraphics(rtPSO);

    // alloate a buffer for the sphere data.
    auto &arena = encoder.getArena();
    auto *sphereBuf = arena.push<StorageBuffer<shd::Sphere>>(this->spheres.size());
    sphereBuf->copyFromHost(this->spheres.data(), this->spheres.size());


    // construct the meshes from the scene.
    ren::RenderWorld world;
    world.extractFromECS(ren::world());

    auto &mmb = ren::resource<ren::MegaMeshBuffer>();

    std::vector<shd::Mesh> meshes;
    for (auto &r : world.renderables) {
      shd::Mesh mesh = {};

      // The shader moves each ray into model space, so the box stays tight
      // when the mesh rotates.
      auto &bb = r.mesh->getAABB();
      mesh.worldToModel = glm::inverse(r.transform);
      mesh.minBounds = bb.min;
      mesh.maxBounds = bb.max;

      auto entry = mmb.getEntry(r.mesh->megaHandle);
      mesh.indexOffset = entry.indexOffset;
      mesh.indexCount = entry.indexCount;
      mesh.vertexOffset = entry.vertexOffset;
      meshes.push_back(mesh);
    }

    auto *meshBuf = arena.push<StorageBuffer<shd::Mesh>>(meshes.size());
    meshBuf->copyFromHost(meshes.data(), meshes.size());
    //


    auto root = shader.root("root");
    root.set("randomSeed", randomizeSeed ? static_cast<u32>(rand()) : randomSeed);
    root.set("maxBounces", this->maxBounces);
    root.set("raysPerPixel", this->raysPerPixel);
    root.set("numSpheres", static_cast<u32>(this->spheres.size()));
    root.set("spheres", sphereBuf->getGPUAddress());
    root.set("accumulatedFrames", accumulatedFrames);
    root.set("accumulation", accumulation->getGPUAddress());
    root.set("skyboxHdri", skybox);

    root.set("vertices", mmb.getVertexBuffer().getGPUAddress());
    root.set("indices", mmb.getIndexBuffer().getGPUAddress());
    root.set("numMeshes", static_cast<u32>(meshes.size()));
    root.set("meshes", meshBuf->getGPUAddress());

    shader.drawFullscreenTriangle();

    if (accumulate) {
      accumulatedFrames++;
    }
  }


  void inspect(void) {
    ImGui::Text("Raytracer viewport");
    ImGui::SliderInt("Max Bounces", reinterpret_cast<int *>(&maxBounces), 1, 10);
    ImGui::SliderInt("Rays Per Pixel", reinterpret_cast<int *>(&raysPerPixel), 1, 10);
    ImGui::Checkbox("Accumulate", &accumulate);
    ImGui::SameLine();
    ImGui::Text("(%u frames)", accumulatedFrames);

    ImGui::Separator();
    // Spheres
    for (auto &sphere : spheres) {
      ImGui::PushID(&sphere);
      ImGui::DragFloat3("Position", &sphere.position.x, 0.1f);
      ImGui::DragFloat("Radius", &sphere.radius, 0.1f, 0.1f, 10.0f);
      ImGui::ColorEdit3("Color", &sphere.material.color.x);
      ImGui::ColorEdit3("Emission Color", &sphere.material.emissionColor.x);
      ImGui::SliderFloat("Emission Strength", &sphere.material.emissionColor.w, 0.0f, 10.0f);
      ImGui::SliderFloat("Smoothness", &sphere.material.smoothness, 0.0f, 1.0f);
      ImGui::PopID();
      ImGui::Separator();
      ImGui::Spacing();
    }
    if (ImGui::Button("Add Sphere")) {
      spheres.push_back({{0.0f, 0.0f, 0.0f}, 1.0f, {{1.0f, 1.0f, 1.0f, 1.0f}, {0, 0, 0, 0.0f}}});
    }

    if (ImGui::Button("Dump Spheres")) {
      // Print each sphere as valid c++ code to the console.
      // This is so I can edit the spheres, but then copy them into the code to make them permanent.
      for (auto &sphere : spheres) {
        printf("spheres.push_back({{%f, %f, %f}, %f, {{%f, %f, %f, %f}, {%f, %f, %f, %f}}});\n", sphere.position.x, sphere.position.y,
               sphere.position.z, sphere.radius, sphere.material.color.x, sphere.material.color.y, sphere.material.color.z, sphere.material.color.w,
               sphere.material.emissionColor.x, sphere.material.emissionColor.y, sphere.material.emissionColor.z, sphere.material.emissionColor.w);
      }
    }
  }

  void onBeforeGraph(ren::CommandEncoder &encoder) override {
    const glm::uvec2 size = getRenderSize();
    const size_t pixelCount = static_cast<size_t>(size.x) * size.y;
    if (!accumulation || accumulation->count() != pixelCount) {
      // Earlier frames can still be using the old buffer. Like a graph image
      // resize, stall until they finish before the buffer is freed.
      getVulkan().waitForIdle();
      accumulation = ren::makeBox<StorageBuffer<glm::vec4>>(pixelCount);
    }

    const bool cameraStill = nearlyEqual(view, lastView) && nearlyEqual(projection, lastProjection);
    const bool sceneSame = size == lastSize && maxBounces == lastMaxBounces;
    if (!accumulate || !cameraStill || !sceneSame) {
      accumulatedFrames = 0;
    }
    lastView = view;
    lastProjection = projection;
    lastSize = size;
    lastMaxBounces = maxBounces;

    // The previous frame's fragment shader wrote the buffer, and this frame's
    // fragment shader reads and writes it again.
    const VkMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
    };
    const VkDependencyInfo dependency{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &barrier,
    };
    vkCmdPipelineBarrier2(encoder.buf(), &dependency);
  }
};


class RaytraceWindow : public ren::Window {
 public:
  void onInitialize(void) override {
    m_viewport = ren::makeBox<RaytraceViewport>();
    m_viewport->position = {0.0f, 1.5f, 4.5f};
    m_viewport->angles = {-0.08f, 0.0f, 0.0f};
    m_viewport->showInputOverlay = true;
  }

  void onUpdate(float dt) override {
    m_deltaTime = dt;
    cam.update(dt);
    m_framerateCounter.addFrame(dt);


    // TODO: this code duplication is pretty hacky.
    auto &frame = ren::getFrameUnit();
    const auto &backbuffer = getSwapchain().currentImage();
    const glm::vec2 frameSize{backbuffer->getWidth(), backbuffer->getHeight()};
    auto viewMatrix = cam.view_matrix();
    auto projMatrix = ren::Camera::projectionMatrix(frameSize.x, frameSize.y);
    frame.setFrameGlobals({
        .time = ren::timeSeconds(),
        .deltaTime = dt,
        .frameNumber = static_cast<u32>(ren::getVulkan().frame_number),
        .renderSize = frameSize,
        .inverseRenderSize = 1.0f / frameSize,
        .viewMatrix = viewMatrix,
        .projMatrix = projMatrix,
    });


    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);


    ImGui::Begin("Raytracer Controls");

    ImGui::Text("Framerate: %.1f FPS", m_framerateCounter.getAverageFramerate());

    ImGui::Separator();
    m_viewport->inspect();
    ImGui::End();

    // The ECS systems calculate the world matrix of each transform.
    auto &world = ren::world();
    REN_PROFILE_EXPR("World Tick", world.progress(dt));
  }


  void onRender(FrameContext &frame) override {
    REN_PROFILE_SCOPE("onRender");
    auto &renderer = ren::Renderer::get();

    m_viewport->drawWindow("Viewport A", m_deltaTime);
    m_viewport->render(renderer);

    auto encoder = frame.getMainCommandEncoder();
    auto timestampTicket = encoder->beginTimestampQuery("FullScreenPass");
    auto pass = encoder->beginRenderPass(*renderer.getDisplayPass(), getSwapchain().currentTarget());
    drawImGui(pass);
    pass.end();
    encoder->endTimestampQuery(timestampTicket);
  }

 private:
  ren::Box<RaytraceViewport> m_viewport;
  ren::FramerateCounter m_framerateCounter;
  ren::Camera cam;
  float m_deltaTime = 0.0f;
};




int main(int argc, char *argv[]) {
  ren::parseFlags(argc, argv);


  REN_PROFILE_BEGIN("raytracer");
  glm::uvec2 res;
  res.x = 1920;
  res.y = 1080;

  ren::run<RaytraceWindow>("Raytracer", glm::uvec2(1920, 1080));


  REN_PROFILE_END_SESSION();

  return 0;
}
