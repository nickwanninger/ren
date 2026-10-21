
#include <ren/core/Instrumentation.h>
#include <ren/core/Application.h>
#include <ren/renderer/graph/RenderGraph.h>
#include <ren/renderer/shader/ShaderReflection.h>

#include <ren/core/Flag.h>
#include <ren/assets/MeshScene.hpp>
#include "ren/renderer/RenderWorld.h"
#include <ren/core/ThreadPool.h>
#include <ren/misc/resource_usage.h>

#include <ren/core/Systems.h>
#include <ren/core/Components.h>
#include <ren/renderer/submission/SubmissionUnit.h>
#include <ren/core/Result.h>
#include <ren/renderer/Viewport.h>
#include <ren/renderer/graph/tasks/BloomTask.h>
#include <cstring>



using namespace ren;


static ren::Flag<float> renderScale("render-scale", 0.25, "raytracing scale");
static ren::Flag<std::string> skyBoxPath("skybox", "assets/hdri/sky.jpg", "Path to the skybox texture to use in the viewport.");
static ren::Flag<std::string> scenePath("scene", "assets/test/meshes/simple_scene.glb", "Path to the scene mesh");
static ren::Flag<float> scaleArg("scene-scale", 1.0f, "Uniform scale to apply to the loaded mesh");



namespace shd {

  // These are shader structures we need to be careful with it.
  struct RayTracingMaterial {
    glm::vec4 color;
    glm::vec4 emissionColor = glm::vec4(0.0f);
    float smoothness = 0.0f;
    // Packed bindless TextureHandle. 0 means no texture.
    u32 albedoTexture = 0;
    float _pad[2] = {};
  };
  static_assert(sizeof(RayTracingMaterial) == 48);

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
    u32 opaque = 1;
    // Device addresses of the ren::MeshBVH nodes and triangle list.
    u64 bvhNodes;
    u64 bvhTriangles;
    u64 _pad2;
    RayTracingMaterial material;
  };
  static_assert(sizeof(Mesh) == 176);
  static_assert(offsetof(Mesh, material) == 128);
  static_assert(offsetof(Mesh, minBounds) == 64);
  static_assert(offsetof(Mesh, indexOffset) == 76);
  static_assert(offsetof(Mesh, maxBounds) == 80);
  static_assert(offsetof(Mesh, indexCount) == 92);
  static_assert(offsetof(Mesh, vertexOffset) == 96);
  static_assert(offsetof(Mesh, bvhNodes) == 104);

}  // namespace shd

class RaytraceViewport : public ren::EditorViewport {
 public:
  PipelineStateObject rtPSO;

  u32 debugView = 0;
  u32 debugTestScale = 2000;
  float exposure = 0.0f;
  BloomCompositeTask *bloom = nullptr;
  ref<Texture> skybox;
  GraphHandle previousScene;
  ref<RenderWorld> preparedWorld;

  void buildGraph() override {
    ren::MeshScene::load("assets/test/meshes/unit_cube.glb")->instantiate({});
    auto e = ren::MeshScene::load(scenePath.get())->instantiate({});
    e.get_mut<ren::comp::Transform>().scale = glm::vec3(scaleArg.get());


    this->renderScale = ::renderScale.get();


    this->skybox = ren::Texture::load(skyBoxPath.get(), VK_FORMAT_R8G8B8A8_SRGB);

    rtPSO.debugName = "Raytracer";
    rtPSO.program = ren::getShader("raytrace", "ren/pass/fullscreen");
    rtPSO.hasVertexBinding = false;
    rtPSO.cullMode = ren::CullMode::None;
    rtPSO.depthTest = false;
    rtPSO.depthWrite = false;

    GraphHandle sceneColor;
    graph.pass("raytrace")
        .createColorAttachment("sceneColor", {.size = ImageSize::viewRelative(1.0f), .format = VK_FORMAT_R16G16B16A16_SFLOAT, .temporal = true},
                               sceneColor)
        .prepare([this](PrepareContext &ctx) {
          preparedWorld = make<RenderWorld>();
          preparedWorld->extractFromECS(ren::world());
          ctx.retain(preparedWorld);
        })
        .render([this](GraphRenderPassContext &ctx) { this->runRaytracer(ctx.encoder); });
    previousScene = graph.graph().previous(sceneColor);
    bloom = &addBloom(graph, sceneColor, finalImage);
  }

  void runRaytracer(ren::RenderPassEncoder &encoder) {
    auto shader = encoder.bindGraphics(rtPSO);

    auto &arena = encoder.getArena();


    // construct the meshes from the scene.
    const auto &world = *preparedWorld;

    auto &mmb = ren::resource<ren::MegaMeshBuffer>();

    std::vector<shd::Mesh> meshes;
    // World-space box of each mesh, for the top-level BVH.
    std::vector<ren::AABB> meshBoxes;
    for (auto &r : world.renderables) {
      if (r.mesh->getBVH() == nullptr) {
        continue;
      }
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
      mesh.bvhNodes = r.mesh->getBVH()->getNodeAddress();
      mesh.bvhTriangles = r.mesh->getBVH()->getTriangleAddress();
      mesh.material.color = glm::vec4(1.0f);
      if (r.material) {
        if (const auto *schema = r.material->getSchema()) {
          const auto &data = r.material->getCpuMaterialData();
          auto readField = [&](const auto &field, auto &value) {
            if (field.scalarKind == ShaderReflection::ScalarKind::Float32 && field.byteSize == sizeof(value) && field.byteOffset <= data.size() &&
                sizeof(value) <= data.size() - field.byteOffset) {
              std::memcpy(&value, data.data() + field.byteOffset, sizeof(value));
            }
          };
          for (const auto &field : schema->fields) {
            if (field.name == "baseColor") {
              readField(field, mesh.material.color);
            } else if (field.name == "baseColorTexture" && field.byteSize == sizeof(u32) && field.byteOffset <= data.size() &&
                       sizeof(u32) <= data.size() - field.byteOffset) {
              std::memcpy(&mesh.material.albedoTexture, data.data() + field.byteOffset, sizeof(u32));
            }
          }
        }
      }
      meshes.push_back(mesh);

      ren::AABB worldBox(glm::vec3(INFINITY), glm::vec3(-INFINITY));
      for (int corner = 0; corner < 8; corner++) {
        glm::vec3 p((corner & 1) ? bb.max.x : bb.min.x, (corner & 2) ? bb.max.y : bb.min.y, (corner & 4) ? bb.max.z : bb.min.z);
        glm::vec3 worldPos = glm::vec3(r.transform * glm::vec4(p, 1.0f));
        worldBox.min = glm::min(worldBox.min, worldPos);
        worldBox.max = glm::max(worldBox.max, worldPos);
      }
      meshBoxes.push_back(worldBox);
    }

    auto *meshBuf = arena.push<StorageBuffer<shd::Mesh>>(meshes.size());
    meshBuf->copyFromHost(meshes.data(), meshes.size());

    // The top-level BVH over the meshes. The transforms can change each
    // frame, so build it each frame.
    u64 sceneNodes = 0;
    u64 sceneEntries = 0;
    if (!meshBoxes.empty()) {
      ren::BoundsBVH sceneBVH = ren::buildBoundsBVH(meshBoxes);
      auto *nodeBuf = arena.push<StorageBuffer<ren::BVHNode>>(sceneBVH.nodes.size());
      nodeBuf->copyFromHost(sceneBVH.nodes.data(), sceneBVH.nodes.size());
      auto *entryBuf = arena.push<StorageBuffer<u32>>(sceneBVH.entries.size());
      entryBuf->copyFromHost(sceneBVH.entries.data(), sceneBVH.entries.size());
      sceneNodes = nodeBuf->getGPUAddress();
      sceneEntries = entryBuf->getGPUAddress();
    }


    auto root = shader.root("root");
    root.set("skyboxHdri", skybox);

    root.set("vertices", mmb.getVertexBuffer().getGPUAddress());
    root.set("indices", mmb.getIndexBuffer().getGPUAddress());
    root.set("numMeshes", static_cast<u32>(meshes.size()));
    root.set("meshes", meshBuf->getGPUAddress());
    root.set("sceneNodes", sceneNodes);
    root.set("sceneEntries", sceneEntries);


    root.set("debugView", debugView);
    root.set("debugTestScale", debugTestScale);
    bloom->exposure = exposure;
    bloom->bypass = debugView != 0;

    shader.drawFullscreenTriangle();
  }


  void inspect(void) {
    ImGui::Text("Raytracer viewport");
    const auto size = getRenderSize();
    ImGui::Text("Primary rays per frame: %zu", static_cast<size_t>(size.x) * size.y);
    ImGui::SliderFloat("Exposure (stops)", &exposure, -10.0f, 10.0f, "%.2f");
    if (bloom) {
      bloom->inspect();
    }
    ImGui::SeparatorText("Debug Views");

    ImGui::SliderInt("Debug View", reinterpret_cast<int *>(&debugView), 0, 2);
    if (debugView != 0) {
      if (debugView == 1) {
        ImGui::Text("Debug View: Triangle Tests");
      } else if (debugView == 2) {
        ImGui::Text("Debug View: Bounds Tests");
      }
      ImGui::DragInt("Debug Test Scale", reinterpret_cast<int *>(&debugTestScale), 1, 10000);
    }
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


    {
      // Sum usage and budget over the device-local heaps. vmaGetHeapBudgets writes one entry per heap.
      VmaAllocator allocator = ren::getVulkan().allocator;
      const VkPhysicalDeviceMemoryProperties *memProps = nullptr;
      vmaGetMemoryProperties(allocator, &memProps);
      VmaBudget budgets[VK_MAX_MEMORY_HEAPS];
      vmaGetHeapBudgets(allocator, budgets);
      VkDeviceSize vramUsage = 0;
      VkDeviceSize vramBudget = 0;
      for (u32 i = 0; i < memProps->memoryHeapCount; i++) {
        vramUsage += budgets[i].usage;
        vramBudget += budgets[i].budget;
      }

      const u64 hostAnonymous = ren::getCurrentProcessAnonymousRSS();
      char buf[192];
      snprintf(buf, sizeof(buf), "%4d FPS DT=%.3f   |   VRAM: %.2f/%.2f MB (%7.1f%%)   |   Host: %.2f MB",
               (int)m_framerateCounter.getAverageFramerate(), dt, vramUsage / 1024.0 / 1024.0, vramBudget / 1024.0 / 1024.0,
               (vramUsage * 100.0) / vramBudget, hostAnonymous / 1024.0 / 1024.0);
      ImGui::Text("%s", buf);
    }


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
    if (m_viewport->previousScene) {
      ImGui::Begin("Previous raytrace result");
      ImGui::Image(renderer.getGraphImages().request(m_viewport->previousScene), ImVec2(384, 216));
      ImGui::End();
    }
    renderer.prepareGraph();
    renderer.recordGraph();

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
