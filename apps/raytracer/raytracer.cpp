
#include <ren/core/Instrumentation.h>
#include <ren/core/Application.h>
#include <ren/renderer/graph/RenderGraph.h>
#include <ren/renderer/shader/ShaderReflection.h>

#include <ren/core/Flag.h>
#include <ren/assets/MeshScene.hpp>
#include <ren/core/ThreadPool.h>

#include <ren/core/Systems.h>
#include <ren/core/Components.h>
#include <ren/renderer/submission/SubmissionUnit.h>
#include <ren/core/Result.h>
#include <ren/renderer/Viewport.h>



using namespace ren;




namespace shd {

  struct RayTracingMaterial {
    glm::vec4 color;
  };

  struct Sphere {
    glm::vec3 position;
    float radius;
    RayTracingMaterial material;
  };

}  // namespace shd

class RaytraceViewport : public ren::EditorViewport {
 public:
  std::vector<shd::Sphere> spheres;
  PipelineStateObject rtPSO;

  void buildGraph() override {


    spheres.push_back({{0.0f, 0.0f, 0.0f}, 1.0f, {{1.0f, 0.0f, 0.0f, 1.0f}}});
    spheres.push_back({{2.0f, 1.5f, 0.0f}, 1.0f, {{0.0f, 1.0f, 0.0f, 1.0f}}});


    this->renderScale = 0.5f;

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
    auto root = shader.root("root");
    root.set("numSpheres", static_cast<u32>(this->spheres.size()));
    root.set("spheres", sphereBuf->getGPUAddress());

    shader.drawFullscreenTriangle();
  }


  void inspect(void) { ImGui::Text("Raytracer viewport"); }
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
