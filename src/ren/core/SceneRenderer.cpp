#include <ren/core/SceneRenderer.h>
#include <ren/renderer/FrameData.h>
#include <ren/core/Instrumentation.h>
#include <ren/assets/Mesh.h>

#include <ren/core/Entity.h>
#include <ren/core/Components.h>

#include <ren/types.h>

namespace ren {


  // This is a simple struct to hold the data of objects we plan on rendering.
  // The scene renderer builds an vector of these then chews through them
  // through the various passes that are needed.
  // Currently, I just render each mesh instance as a separate draw call,
  // but in the future I'll batch them together for performance reasons.
  struct SceneBatch {
    ref<Mesh> mesh;
    glm::mat4 transform;
  };

  SceneRenderer::SceneRenderer(Renderer &R)
      : R(R) {
    // Initialize any necessary resources for the SceneRenderer.

    initOpaque();
  }


  void SceneRenderer::initOpaque(void) {
    REN_PROFILE_FUNCTION();

    // Initialize the opaque pass description.

    // HDR emissive data.
    opaque.passDesc.addColorAttachment("emissive", VK_FORMAT_R8G8B8A8_UNORM);
    // Albedo/diffuse color data.
    opaque.passDesc.addColorAttachment("albedo", VK_FORMAT_R8G8B8A8_UNORM);
    // World space normal data.
    opaque.passDesc.addColorAttachment("normal", VK_FORMAT_R16G16B16A16_SNORM);
    // PBR data (R= metallic, G=roughness, B=?).
    opaque.passDesc.addColorAttachment("pbr", VK_FORMAT_R8G8B8A8_UNORM);
    opaque.passDesc.addDepthAttachment("depthStencil");

    opaque.pass = R.getRenderPassCache().get(opaque.passDesc);

    opaque.pso.program = makeRef<ShaderProgram>("shaders/opaque");
  }


  void SceneRenderer::initLighting(void) {
    REN_PROFILE_FUNCTION();

    // Initialize the lighting pass description.
    lighting.passDesc.name = "Lighting Pass";
    lighting.passDesc.addColorAttachment("lighting", VK_FORMAT_R8G8B8A8_UNORM);
    lighting.passDesc.addDepthAttachment("depthStencil");

    lighting.pass = R.getRenderPassCache().get(lighting.passDesc);

    lighting.pso.program = makeRef<ShaderProgram>("shaders/lighting");
  }


  void SceneRenderer::rebuildRenderTargets(glm::vec2 res) {
    REN_PROFILE_FUNCTION();
    // HACK: this should be done elsewhere through an event system
    //       (or, I should have a real render graph (ie: finish the one I started))
    R.waitForIdle();

    auto rpCache = R.getRenderPassCache();

    float width = res.x;
    float height = res.y;
    if (width == 0) width = 1;
    if (height == 0) height = 1;


    // Go through all the render targets and build them
    opaque.target = opaque.pass->createRenderTarget(width, height);

    this->renderResolution = res;
  }

  ref<RenderTarget> SceneRenderer::render(Scene &scene, Camera &camera) {
    REN_PROFILE_FUNCTION();

    // get the current frame data.
    auto &frame = ren::getFrameData();
    auto &cmd = frame.commandBuffer;


    glm::vec2 outputResolution(frame.renderTarget->getWidth(), frame.renderTarget->getHeight());
    glm::vec2 currentRenderResolution = outputResolution * renderScale;

    if (currentRenderResolution != this->renderResolution) {
      // Rebuild the render targets if the resolution has changed.
      rebuildRenderTargets(currentRenderResolution);
    }

    // Don't render!
    if (renderResolution.x == 0 || renderResolution.y == 0) {
      return opaque.target;
    }

    // Update all the transformation matrices in the scene to be global.
    scene.globalizeTransforms();

    std::vector<SceneBatch> batches;

    auto view = scene.getAllWith<comp::Mesh, comp::Transform>();
    view.each([&](entt::entity id, const comp::Mesh &mesh, const comp::Transform &transform) {
      // Create a batch for each mesh instance.
      SceneBatch batch;
      batch.mesh = mesh.mesh;
      batch.transform = transform.transformMatrix;
      batches.push_back(std::move(batch));
    });


    float renderAspect = (float)renderResolution.x / (float)renderResolution.y;
    camera.projection = glm::perspective(glm::radians(90.0f), renderAspect, 0.01f, 1000.0f);
    camera.projection[1][1] *= -1;  // Vulkan thing.

    // Begin the opaque render pass.
    frame.perf.begin(cmd, "Opaque Pass");
    R.withPass(*opaque.pass, *opaque.target, [&]() {
      REN_PROFILE_SCOPE("Opaque Pass");


      R.bind(opaque.pso);
      ren::MeshPushConstants pc;
      pc.view = camera.view_matrix();
      pc.proj = camera.projection;

      for (auto &batch : batches) {
        ren::bind(cmd, *batch.mesh->getIndexBuffer());
        ren::bind(cmd, *batch.mesh->getVertexBuffer());

        pc.model = batch.transform;
        R.setPushConstants(pc);

        vkCmdDrawIndexed(cmd, batch.mesh->getIndexCount(), 1, 0, 0, 0);
      }

    });
    frame.perf.end(cmd, "Opaque Pass");



    return opaque.target;
  }


  void SceneRenderer::inspect(void) {
    // TODO:
  }


}  // namespace ren
