#pragma once


#include <ren/types.h>
#include <ren/layers/Layer.h>
#include <ren/core/Scene.h>
#include <ren/Camera.h>
#include <ren/assets/MeshScene.hpp>

namespace ren {
  class SceneLayer : public Layer {
   public:
    ren::Scene scene;
    ren::Camera camera;

    // TODO: SceneRenderer

    ref<MeshScene> meshScene;


    ren::Entity selectedEntity = {}; // Null for now!


    SceneLayer(Application &app);
    ~SceneLayer() override = default;

    void onUpdate(float deltaTime) override;
    void onAttach(void) override;
    void onDetach(void) override;
    void onEvent(Event &event) override;
    void onImguiRender(float deltaTime) override;

    private:
    void renderEntityHeirarchy(Entity entity);
  };
}  // namespace ren