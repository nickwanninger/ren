#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_vulkan.h>
#include <glm/gtc/type_ptr.hpp>

#include <ren/layers/SceneLayer.h>
#include <ren/core/Instrumentation.h>
#include <ren/core/Application.h>
#include <ren/types.h>

#include <ren/renderer/pipelines/PipelineStateDesc.h>

namespace ren {

  SceneLayer::SceneLayer(Application &app)
      : Layer(app, "ImGui") {}


  void SceneLayer::onAttach(void) {
    REN_PROFILE_FUNCTION();
    fmt::print("Scene Layer Attached\n");

    Entity cube = scene.createEntity("Cube 1");
    cube.add<comp::Mesh>(ren::loadGLTF("assets/test/meshes/unit_cube.glb"));


    Entity cube2 = scene.createEntity("Cube 2");
    cube2.add<comp::Mesh>(ren::loadGLTF("assets/test/meshes/unit_cube.glb"));
    cube2.translation().y = 1.0f;


    ren::PipelineStateDesc desc;


    //
  }

  void SceneLayer::onUpdate(float deltaTime) {
    REN_PROFILE_FUNCTION();

    camera.update(deltaTime);
  }


  void SceneLayer::onDetach(void) {
    REN_PROFILE_FUNCTION();

    fmt::print("Scene Layer Detached\n");
    // Cleanup the render target for the current scene.
  }

  void SceneLayer::onEvent(Event &event) {
    REN_PROFILE_FUNCTION();
    // Handle events
  }


  void SceneLayer::onImguiRender(float deltaTime) {
    REN_PROFILE_FUNCTION();


    ImGui::Begin("Scene Layer");


    if (ImGui::CollapsingHeader("Scene Information")) {
      ImGui::DragFloat3("Camera Position", glm::value_ptr(camera.position), 0.1f);
      ImGui::DragFloat3("Camera Rotation", glm::value_ptr(camera.angles), 0.1f);
      ImGui::DragFloat3("Camera Velocity", glm::value_ptr(camera.velocity), 0.1f);
    }


    ImGui::Separator();

    if (ImGui::Button("Add Cube")) {
      Entity cube = scene.createEntity("Added Cube");
      cube.add<comp::Mesh>(ren::loadGLTF("assets/test/meshes/unit_cube.glb"));
    }

    auto view = scene.getAllWith<comp::ID, comp::Name, comp::Transform>();
    for (auto [entity, id, name, transform] : view.each()) {
      ImGui::PushID((u32)entity);
      ImGui::Text("Entity: %s (ID: 0x%llx)", name.name.c_str(), (u64)id.uuid);
      ImGui::DragFloat3("Position", glm::value_ptr(transform.translation), 0.1f);
      ImGui::DragFloat3("Rotation", glm::value_ptr(transform.rotation), 0.1f);
      ImGui::DragFloat3("Scale   ", glm::value_ptr(transform.scale), 0.1f);
      ImGui::PopID();
    }

    ImGui::End();
  }
}  // namespace ren
