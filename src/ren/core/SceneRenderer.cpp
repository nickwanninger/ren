#include <ren/core/SceneRenderer.h>
#include <ren/renderer/FrameData.h>
#include <ren/core/Instrumentation.h>
#include <ren/assets/Mesh.h>
#include <imgui.h>
#include <ren/core/Entity.h>
#include <ren/core/Components.h>
#include <ren/core/Application.h>
#include <ren/core/DebugLines.hpp>
#include <ren/types.h>
#include <ImGuizmo/ImGuizmo.h>

namespace ren {


  // This is a simple struct to hold the data of objects we plan on rendering.
  // The scene renderer builds an vector of these then chews through them
  // through the various passes that are needed.
  // Currently, I just render each mesh instance as a separate draw call,
  // but in the future I'll batch them together for performance reasons.
  struct SceneBatch {
    ref<Mesh> mesh;
    ref<Material> material;
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
    // Albedo/diffuse color data and world space normals.
    opaque.passDesc.addColorAttachment("outColor", VK_FORMAT_R16G16B16A16_SFLOAT);
    opaque.passDesc.addColorAttachment("outNormal", VK_FORMAT_R16G16B16A16_SNORM);
    opaque.passDesc.addDepthAttachment("depthStencil");
    opaque.pass = R.getRenderPassCache().get(opaque.passDesc);
  }


  void SceneRenderer::initLighting(void) {
    REN_PROFILE_FUNCTION();

    // Initialize the lighting pass description.
    lighting.passDesc.name = "Lighting Pass";
    lighting.passDesc.addColorAttachment("lighting", VK_FORMAT_R8G8B8A8_UNORM);
    lighting.passDesc.addDepthAttachment("depthStencil");

    lighting.pass = R.getRenderPassCache().get(lighting.passDesc);
  }


  void SceneRenderer::rebuildRenderTargets(glm::vec2 res) {
    REN_PROFILE_FUNCTION();

    fmt::println("Rebuilding render targets to {}, {}", res.x, res.y);

    // HACK: this should be done elsewhere through an event system
    //       (or, I should have a real render graph (ie: finish the one I started))
    R.waitForIdle();

    auto rpCache = R.getRenderPassCache();

    float width = res.x;
    float height = res.y;
    if (width < 1) width = 1;
    if (height < 1) height = 1;


    float targetHeight = 480 / 2;
    targetHeight = height;
    float scale = targetHeight / height;
    width *= scale;
    height *= scale;


    // Go through all the render targets and build them
    opaque.target = opaque.pass->createRenderTarget(width, height);

    this->renderResolution = res;
  }

  ref<RenderTarget> SceneRenderer::render(Scene &scene, Camera &camera) {
    REN_PROFILE_FUNCTION();


    // get the current frame data.
    auto &frame = ren::getFrameData();
    auto &cmd = frame.commandBuffer;

    renderScale = 1.0f;


    glm::vec2 outputResolution(frame.renderTarget->getWidth(), frame.renderTarget->getHeight());
    if (outputResolution.x <= 0 || outputResolution.y <= 0) {
      // fmt::println("Output resolution is zero, skipping render.");
      return nullptr;
      // return opaque.target;
    }

    glm::vec2 currentRenderResolution = outputResolution * renderScale;

    if (currentRenderResolution != this->renderResolution) {
      fmt::println("Output resolution: {}x{}", outputResolution.x, outputResolution.y);
      fmt::println("Current render resolution: {}x{} ({})", currentRenderResolution.x,
                   currentRenderResolution.y, renderScale);

      // Rebuild the render targets if the resolution has changed.
      rebuildRenderTargets(currentRenderResolution);
    }

    // Don't render!
    if (renderResolution.x == 0 || renderResolution.y == 0) { return opaque.target; }

    // Update all the transformation matrices in the scene to be global.
    scene.globalizeTransforms();

    std::unordered_map<ren::Material *, std::unordered_map<ren::Mesh *, std::vector<glm::mat4>>>
        batchesByMaterial;



    std::unordered_set<ren::Mesh *> uniqueMeshes;
    {
      REN_PROFILE_SCOPE("Build Batches");
      ren::world()
          .query<comp::Mesh, comp::Transform, comp::Material>(
              "ren::core::renderer::BatchBuildQuery")
          .each([&](const comp::Mesh &mesh, const comp::Transform &transform,
                    const comp::Material &material) {
            uniqueMeshes.insert(mesh.mesh.get());
            // Create a batch for each mesh instance.
            SceneBatch batch;
            batch.mesh = mesh.mesh;
            batch.transform = transform.transformMatrix;
            batch.material = material.material;

            batchesByMaterial[material.material.get()][mesh.mesh.get()].push_back(batch.transform);
          });
    }

    float renderAspect = (float)renderResolution.x / (float)renderResolution.y;
    camera.projection = glm::perspective(glm::radians(90.0f), renderAspect, 0.01f, 1000.0f);
    camera.projection[1][1] *= -1;  // Vulkan thing.

    // Begin the opaque render pass.
    frame.perf.begin(cmd, "Opaque Pass");


    u64 vertsDrawn = 0;


    R.withPass(*opaque.pass, *opaque.target, [&]() {
      REN_PROFILE_SCOPE("Opaque Pass");

      ren::MeshPushConstants pc;
      pc.view = camera.view_matrix();
      pc.proj = camera.projection;


      auto guizmo_view = pc.view;
      auto guizmo_proj = pc.proj;
      // flip for Vulkan
      guizmo_proj[1][1] *= -1;
      // ImGuizmo::DrawGrid(glm::value_ptr(guizmo_view), glm::value_ptr(guizmo_proj),
      //                    glm::value_ptr(glm::identity<glm::mat4>()), 100.0f);

      engineUBO.view = pc.view;
      engineUBO.proj = pc.proj;
      engineUBO.cameraWorldPosition = glm::vec4(camera.position, 1.0);

      // ren::debugLine(glm::vec3(0, 0, 0), engineUBO.lightDirection * 512.0f, {1, 0, 1}, 4.0f);


      // // TEMPORARY
      // ren::world()
      //     .query<ren::comp::Transform, ren::comp::Mesh>("Transform Editor")
      //     .each([&](ren::comp::Transform &transform, ren::comp::Mesh &mesh) {
      //       // Get the draw list for the current window
      //       ImDrawList *draw_list = ImGui::GetBackgroundDrawList();


      //       auto ProjectToScreen = [&](const glm::vec3 &point, const glm::mat4 &view,
      //                                  const glm::mat4 &proj) {
      //         glm::vec4 clipSpace = proj * view * glm::vec4(point, 1.0f);
      //         clipSpace /= clipSpace.w;
      //         return ImVec2{(clipSpace.x + 1.0f) * 0.5f * ImGui::GetIO().DisplaySize.x,
      //                       (clipSpace.y + 1.0f) * 0.5f * ImGui::GetIO().DisplaySize.y};
      //       };

      //       // Project your 3D points to screen coordinates
      //       ImVec2 p1_screen = ProjectToScreen(glm::vec3(1, 1, 1), pc.view, pc.proj);
      //       ImVec2 p2_screen = ProjectToScreen(glm::vec3(0, 0, 0), pc.view, pc.proj);
      //       fmt::println("Truth: {} {}, {} {}", p1_screen.x, p1_screen.y, p2_screen.x,
      //       p2_screen.y); draw_list->AddLine(p1_screen, p2_screen, IM_COL32(255, 0, 0, 255), 2);

      //       // ImGuizmo::PushID(&transform);
      //       // auto tmat = transform.getTransform();
      //       // ImGuizmo::Manipulate(glm::value_ptr(guizmo_view), glm::value_ptr(guizmo_proj),
      //       //                      ImGuizmo::TRANSLATE | ImGuizmo::ROTATE | ImGuizmo::BOUNDS,
      //       //                      ImGuizmo::WORLD, glm::value_ptr(tmat));

      //       // // decompose
      //       // glm::vec3 skew;
      //       // glm::vec4 perspective;
      //       // glm::decompose(tmat, transform.scale, transform.rotation, transform.translation,
      //       skew,
      //       //                perspective);
      //       // ImGuizmo::PopID();
      //     });

      this->engineUBOBuffer.update(engineUBO);

      ImGui::Begin("Opaque Pass");

      ren::Material *lastMaterial = nullptr;
      ren::Mesh *lastMesh = nullptr;

      for (auto &[material, meshBatches] : batchesByMaterial) {
        REN_PROFILE_SCOPE("Material Batch");

        if (not material->bind(R)) {
          printf("Failed to bind material for mesh\n");
          continue;  // Skip this batch if the material is not ready.
        }

        auto engineBinder = R.startBinding(0);
        engineBinder.bind("engine", this->engineUBOBuffer);
        engineBinder.apply();


        for (auto &[mesh, transforms] : meshBatches) {
          // REN_PROFILE_SCOPE("Mesh Batch");
          ren::bind(cmd, *mesh->getIndexBuffer());
          ren::bind(cmd, *mesh->getVertexBuffer());
          // pc.model = transforms[0];
          // R.setPushConstants(pc);
          // vkCmdDrawIndexed(cmd, mesh->getIndexCount(), transforms.size(), 0, 0, 0);

          int calls = -1;
          for (const auto &transform : transforms) {
            // REN_PROFILE_SCOPE("Draw Call");
            pc.model = transform;
            R.setPushConstants(pc);
            vkCmdDrawIndexed(cmd, mesh->getIndexCount(), 1, 0, 0, 0);
          }
        }
      }

      ren::emit<DebugDrawEvent>({pc.view, pc.proj});

      ImGui::End();
    });

    frame.perf.end(cmd, "Opaque Pass");


    opaque.target->transitionToShaderReadonly(cmd);

    return opaque.target;

    return nullptr;
  }


  void SceneRenderer::inspect(void) {
    // TODO:

    ImGui::Begin("Scene Renderer");
    ImGui::DragFloat4("Light Direction", glm::value_ptr(engineUBO.lightDirection), 0.1f);
    ImGui::DragFloat4("Camera Direction", glm::value_ptr(engineUBO.cameraWorldPosition), 0.1f);
    ImGui::End();
  }


}  // namespace ren
