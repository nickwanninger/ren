#pragma once

#include <ren/renderer/Renderer.h>
#include <ren/renderer/RenderTarget.h>
#include <ren/renderer/pipelines/PipelineStateObject.h>
#include <ren/core/Scene.h>
#include <ren/Camera.h>

namespace ren {


  struct EngineUBO {
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 invViewProj; // inverse(proj * view)

    // These must be vec4 becasue of std140 alignment.
    glm::vec4 cameraWorldPosition;

    glm::vec4 lightDirection = glm::vec4(0.0, 1.0, 1.0, 1.0); // TEMP
    float time;
  };

  // This class is used to render a scene from a given camera's viewpoint.
  // The output of this renderer is a framebuffer texture containing the
  // scene with postprocessing after tonemapping that can be blitted to the screen.
  class SceneRenderer {
   public:
    SceneRenderer(Renderer &renderer);


    SceneRenderer(const SceneRenderer &) = delete;
    SceneRenderer &operator=(const SceneRenderer &) = delete;
    SceneRenderer(SceneRenderer &&) = delete;
    SceneRenderer &operator=(SceneRenderer &&) = delete;


    // render the scene to a render target from the perspective of a camera and return that target.
    ref<RenderTarget> render(Scene &scene, Camera &camera);


    void inspect(void);

    float renderScale = 0.5f;  // Scale the render resolution by this factor.

   private:
    Renderer &R;  // Reference to the renderer that owns this SceneRenderer.

   private:
    // The cached render target resolution for this scene.
    // We default this to a nonsense value so the first time we render we can
    // reinitialize the render targets
    glm::vec2 renderResolution = {-1.0f, -1.0f};
    // Rebuild the render targets based on a new resolution
    void rebuildRenderTargets(glm::vec2 newResolution);

   private:
    EngineUBO engineUBO;
    UniformBufferSet<EngineUBO> engineUBOBuffer;

    // ---- Private data for opaque rendering ---- //
    void initOpaque(void);
    struct {
      RenderPass::Description passDesc;
      ref<RenderPass> pass;


      ren::PipelineStateObject skyboxPSO;

      // The render target for the opaque pass.
      ref<RenderTarget> target;
    } opaque;


    // Lighting pass
    void initLighting(void);
    struct {
      RenderPass::Description passDesc;
      ref<RenderPass> pass;

      // The render target for the lighting pass.
      ref<RenderTarget> target;
    } lighting;
  };


}  // namespace ren