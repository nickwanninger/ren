#pragma once


#include <ren/types.h>
#include <ren/layers/Layer.h>
#include <ren/core/Scene.h>
#include <ren/Camera.h>

namespace ren {
  class SceneLayer : public Layer {
   public:
    ren::Scene scene;
    ren::Camera camera;

    // TODO: SceneRenderer


    SceneLayer(Application &app);
    ~SceneLayer() override = default;

    void onUpdate(float deltaTime) override;
    void onAttach(void) override;
    void onDetach(void) override;
    void onEvent(Event &event) override;
    void onImguiRender(float deltaTime) override;
  };
}  // namespace ren