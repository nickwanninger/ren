#include <ren/core/SceneRenderer.h>
#include <ren/renderer/FrameData.h>
#include <ren/core/Instrumentation.h>
#include <ren/assets/Mesh.h>
#include <imgui/imgui.h>
#include <ren/core/Entity.h>
#include <ren/core/Components.h>
#include <ren/core/Application.h>
#include <ren/core/DebugLines.hpp>
#include <ren/types.h>
#include <ImGuizmo/ImGuizmo.h>
#include <ren/assets/MegaMeshBuffer.h>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <ren/core/DebugLines.hpp>



static float half_to_float(uint16_t h) {
  uint32_t x = (uint32_t)h;
  uint32_t sign = (x & 0x8000) << 16;
  uint32_t exponent = (x & 0x7C00) >> 10;
  uint32_t mantissa = (x & 0x03FF) << 13;

  if (exponent == 0) {
    return *(float *)&sign;
  } else if (exponent == 31) {
    uint32_t bits = sign | 0x7F800000 | mantissa;
    return *(float *)&bits;
  } else {
    uint32_t f = sign | ((exponent + 112) << 23) | mantissa;
    return *(float *)&f;
  }
}

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

    auto normalFormat = VK_FORMAT_R16G16B16A16_SNORM;
    // auto normalFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    opaque.passDesc.addColorAttachment("outNormal", normalFormat);

    // !DEBUG!
    // opaque.passDesc.addColorAttachment("outTangent", normalFormat);
    // opaque.passDesc.addColorAttachment("outBitangent", normalFormat);
    // opaque.passDesc.addColorAttachment("outWorldPosition", normalFormat);
    // opaque.passDesc.addColorAttachment("outComputedNormal", normalFormat);
    // !DEBUG!


    opaque.passDesc.addDepthAttachment("depthStencil");
    opaque.pass = R.getRenderPassCache().get(opaque.passDesc);

    opaque.skyboxPSO.depthTest = false;
    opaque.skyboxPSO.depthWrite = false;
    opaque.skyboxPSO.program = ShaderProgram::makeFullScreenProgram("shaders/skybox.frag");
    opaque.skyboxPSO.hasVertexBinding = false;
    opaque.skyboxPSO.cullMode = ren::CullMode::None;
  }


  void SceneRenderer::initLighting(void) { REN_PROFILE_FUNCTION(); }


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


    float targetHeight = 480;
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
    camera.projection = ren::Camera::projectionMatrix(renderResolution.x, renderResolution.y);

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
      engineUBO.invViewProj = glm::inverse(pc.proj * pc.view);
      engineUBO.cameraWorldPosition = glm::vec4(camera.position, 1.0);

      engineUBO.time = ren::Application::get().timeSeconds;


      float azimuth = atan2(engineUBO.lightDirection.x,
                            engineUBO.lightDirection.y);  // radians, 0 = north, π/2 = east
      ImGui::Begin("Light Direction");
      ImGui::DragFloat4("Direction", glm::value_ptr(engineUBO.lightDirection), 0.1f);
      if (ImGui::DragFloat("Azimuth (radians)", &azimuth, 0.01f)) {
        float radius =
            glm::length(glm::vec2(engineUBO.lightDirection.x, engineUBO.lightDirection.y));
        engineUBO.lightDirection.x = radius * sin(azimuth);
        engineUBO.lightDirection.y = radius * cos(azimuth);
      }
      ImGui::End();




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




      // TEMP: render a skybox
      this->engineUBOBuffer.update(engineUBO);

      {
        R.bind(opaque.skyboxPSO);
        R.startBinding(0).bind("engine", this->engineUBOBuffer).apply();
        vkCmdDraw(cmd, 3, 1, 0, 0);
      }




      auto &megaMesh = ren::world().get_mut<ren::MegaMeshBuffer>();

      megaMesh.bind(cmd);


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
          auto &entry = megaMesh.getEntry(mesh->megaHandle);

          int calls = -1;
          for (const auto &transform : transforms) {
            pc.model = transform;
            pc.normalMatrix = glm::transpose(glm::inverse(pc.model));
            R.setPushConstants(pc);
            auto instanceCount = 1;
            vkCmdDrawIndexed(cmd, entry.indexCount, instanceCount, entry.indexOffset,
                             entry.vertexOffset, 0);
          }
        }
      }



      ren::emit<DebugDrawEvent>({pc.view, pc.proj});
    });

    frame.perf.end(cmd, "Opaque Pass");

    opaque.target->transitionToShaderReadonly(cmd);



    ////////////// DEBUG ////////////////
    if (0) {
      ImGui::Begin("Render Targets");
      static ren::Sampler sampler;
      struct GPUPixel {
       public:
        uint16_t v[4];
        float r() const { return half_to_float(v[0]); }
        float g() const { return half_to_float(v[1]); }
        float b() const { return half_to_float(v[2]); }
      };

      static ren::FixedUsageTypedBuffer<GPUPixel, VK_BUFFER_USAGE_TRANSFER_DST_BIT>
          pixelReadbackBuffer(32);


      glm::vec2 mousePosition;
      // get the mouse position in normalized coordinates.
      {
        ImGuiIO &io = ImGui::GetIO();
        mousePosition.x = io.MousePos.x / ImGui::GetMainViewport()->Size.x;
        mousePosition.y = io.MousePos.y / ImGui::GetMainViewport()->Size.y;
      }


      long off = 0;
      for (auto &att : opaque.target->getAttachments()) {
        // schedule the read.
        att.texture->readPixelToBuffer(cmd, mousePosition, pixelReadbackBuffer.getHandle(),
                                       off * sizeof(GPUPixel));
        off++;
      }

      // ren::getVulkan().waitForIdle();  // BAD!

      // compute a width for the images that fits in the window.
      float imageWidth = ImGui::GetContentRegionAvail().x;
      // compute a height based on the render resolution aspect ratio.
      float aspect = renderResolution.x / renderResolution.y;
      float imageHeight = imageWidth / aspect;
      off = 0;


      GPUPixel *pixels = pixelReadbackBuffer.map();


      std::map<std::string, glm::vec3> values;
      off = 0;
      for (auto &att : opaque.target->getAttachments()) {
        ImGui::Text("%s", att.name.c_str());
        if (att.imguiTextureID == VK_NULL_HANDLE) {
          att.imguiTextureID =
              ImGui_ImplVulkan_AddTexture(sampler.getHandle(), att.texture->getImageView(),
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
          continue;
        }


        auto &p = pixels[off++];
        ImGui::Text("%f %f %f", p.r(), p.g(), p.b());
        values[att.name] = glm::vec3(p.r(), p.g(), p.b());
        // ImGui::Image(att.imguiTextureID, ImVec2(imageWidth, imageHeight), ImVec2(0, 0),
        //              ImVec2(1, 1));
      }

      auto center = values["outWorldPosition"];
      ImGui::Text("World Position at center: %f %f %f", center.x, center.y, center.z);
      debugLine(center, center + glm::normalize(values["outNormal"]), {1, 0, 0}, 5.0f);
      // debugLine(center, center + glm::normalize(values["outTangent"]), {0, 1, 0}, 5.0f);
      // debugLine(center, center + glm::normalize(values["outBitangent"]), {0, 0, 1}, 5.0f);
      // debugLine(center, center + glm::normalize(values["outComputedNormal"]), {1, 0, 1}, 5.0f);

      auto dot = glm::dot(glm::normalize(values["outNormal"]),
                          glm::normalize(values["outComputedNormal"]));
      // show a progress bar of the dot product
      ImGui::ProgressBar(dot, ImVec2(0.0f, 0.0f));
      // ImGui::Text("Normal vs Computed Normal Dot Product: %f", dot);


      pixelReadbackBuffer.unmap();
      ImGui::End();
      ////////////// DEBUG ////////////////
    }

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
