#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_vulkan.h>
#include <imgui_internal.h>
#include <ImGuizmo/ImGuizmo.h>



#include <ren/layers/SceneLayer.h>
#include <ren/layers/inspector/Inspector.h>
#include <ren/core/Instrumentation.h>
#include <ren/core/Application.h>
#include <ren/assets/MeshScene.hpp>

#include <ren/types.h>
#include <ren/core/DebugLines.hpp>
#include <ren/core/AutoPlugin.h>




namespace ren {

  struct SceneSelectedEntity {
    char _unused;
  };

  void draw_guizmo_editor(ren::Application &app) {
    return;
    ren::onEvent<DebugDrawEvent>([&](DebugDrawEvent &event) {
      ren::world()
          .query<SceneSelectedEntity, ren::comp::Transform>("ren::editor::GizmoQuery")
          .each([&](SceneSelectedEntity, ren::comp::Transform &transform) {
            auto guizmo_view = event.view;
            auto guizmo_proj = event.proj;
            // flip for Vulkan
            guizmo_proj[1][1] *= -1;

            ImGuizmo::PushID(&transform);
            auto tmat = transform.getTransform();
            ImGuizmo::Manipulate(glm::value_ptr(guizmo_view), glm::value_ptr(guizmo_proj),
                                 ImGuizmo::TRANSLATE | ImGuizmo::ROTATE | ImGuizmo::BOUNDS,
                                 ImGuizmo::WORLD, glm::value_ptr(tmat));

            // decompose
            glm::vec3 skew;
            glm::vec4 perspective;
            glm::decompose(tmat, transform.scale, transform.rotation, transform.translation, skew,
                           perspective);
            ImGuizmo::PopID();
          });
    });
  }
  REN_PLUGIN("GuizmoEditor", draw_guizmo_editor);


  SceneLayer::SceneLayer(Application &app)
      : Layer(app, "ImGui")
      , scene(app.world.lookup("scene")) {
    this->selectedEntity = {};
  }


  void SceneLayer::onAttach(void) {
    REN_PROFILE_FUNCTION();
    fmt::print("Scene Layer Attached\n");


    // this->meshScene = MeshScene::load("assets/test/meshes/simple_scene.glb");
    // this->meshScene = MeshScene::load("/Users/nick/Desktop/sponza.glb");
    // this->meshScene = MeshScene::load("/Users/nick/Downloads/NormalTangentTest.glb");
    // this->meshScene = MeshScene::load("/Users/nick/Downloads/DamagedHelmet.glb");
    // this->meshScene =
    // MeshScene::load("/Users/nick/Downloads/pkg_a_curtains/NewSponza_Curtains_glTF.gltf");
    // this->meshScene = MeshScene::load("assets/test/meshes/unit_cube.glb");
    // this->meshScene = MeshScene::load("/Users/nick/Downloads/MetalRoughSpheres.glb");
    // this->meshScene = MeshScene::load("/Users/nick/Downloads/test3.glb");

    // ren::Entity e = this->meshScene->instantiate(scene);

    // // fmt::println("JSON:", e.to_json().c_str());
    // e.get_mut<comp::Transform>().scale = glm::vec3(1.0f); // Set the scale of the instantiated
    // entity
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


  static bool drawVec3Control(const std::string &label, glm::vec3 &values, float resetValue = 0.0f,
                              float columnWidth = 100.0f) {
    bool changed = false;
    ImGui::PushID(&values);
    ImGui::Columns(2);

    ImGui::SetColumnWidth(0, columnWidth);
    // set the next column to the full width
    ImGui::Text("%s", label.c_str());
    ImGui::NextColumn();


    ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0, 0});

    float lineheight = ImGui::GetFrameHeightWithSpacing();
    ImVec2 buttonSize = ImVec2{lineheight + 3, lineheight};


    // ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.8f, 0.1f, 0.15f, 1.0f});
    // ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.9f, 0.2f, 0.2f, 1.0f});
    // ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.8f, 0.1f, 0.15f, 1.0f});
    if (ImGui::Button("X", buttonSize)) {
      values.x = resetValue;
      changed = true;
    }
    // ImGui::PopStyleColor(3);

    ImGui::SameLine();
    changed |= ImGui::DragFloat("##X", &values.x, 0.1f);
    ImGui::PopItemWidth();
    ImGui::SameLine();



    // ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.2f, 0.7f, 0.3f, 1.0f});
    // ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.2f, 0.9f, 0.3f, 1.0f});
    // ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.2f, 0.7f, 0.3f, 1.0f});
    if (ImGui::Button("Y", buttonSize)) {
      values.y = resetValue;
      changed = true;
    }
    // ImGui::PopStyleColor(3);

    ImGui::SameLine();
    changed |= ImGui::DragFloat("##Y", &values.y, 0.1f);
    ImGui::PopItemWidth();
    ImGui::SameLine();



    // ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.1f, 0.25f, 0.8f, 1.0f});
    // ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.2f, 0.35f, 0.9f, 1.0f});
    // ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.1f, 0.25f, 0.8f, 1.0f});
    if (ImGui::Button("Z", buttonSize)) {
      values.z = resetValue;
      changed = true;
    }
    // ImGui::PopStyleColor(3);

    ImGui::SameLine();
    changed |= ImGui::DragFloat("##Z", &values.z, 0.1f);
    ImGui::PopItemWidth();


    ImGui::PopStyleVar();
    ImGui::Columns(1);
    ImGui::PopID();
    return changed;
  }



  void SceneLayer::renderEntityHeirarchy(Entity entity) {
    REN_PROFILE_FUNCTION();

    u64 eid = entity.id();
    if (entity.has<comp::ID>()) { eid = entity.get<comp::ID>().uuid; }
    ImGui::PushID(eid);
    // Check if the entity has children
    bool hasChildren = false;
    // there has to be a better way to do this
    entity.children([&](Entity) { hasChildren = true; });

    ImGuiTreeNodeFlags nodeFlags =
        ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_OpenOnArrow;
    if (!hasChildren) nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    bool isSelected = (this->selectedEntity == entity);
    if (isSelected) {
      ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(40, 120, 255, 255));
      nodeFlags |= ImGuiTreeNodeFlags_Selected;
    }

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);

    bool nodeOpen = ImGui::TreeNodeEx(entity.get<comp::Name>().name.c_str(), nodeFlags);

    // Only select on row click, not arrow click
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
      if (selectedEntity && selectedEntity.has<SceneSelectedEntity>()) {
        selectedEntity.remove<SceneSelectedEntity>();
      }
      this->selectedEntity = entity;
      selectedEntity.emplace<SceneSelectedEntity>();
    }

    ImGui::TableSetColumnIndex(1);
    u32 entity_id = static_cast<u32>(eid);
    u32 entity_generation = eid >> 32;
    ImGui::Text("%u.%u", entity_id, entity_generation);

    if (isSelected) { ImGui::PopStyleColor(); }

    if (hasChildren && nodeOpen) {
      entity.children([this](Entity child) { renderEntityHeirarchy(child); });
      ImGui::TreePop();
    }

    ImGui::PopID();
  }


  void SceneLayer::onImguiRender(float deltaTime) {
    REN_PROFILE_FUNCTION();

    auto viewMatrix = this->camera.view_matrix();
    auto projMatrix = this->camera.projection;
    projMatrix[1][1] *= -1;  // Vulkan thing, flip the Y axis.
    auto identityMatrix = glm::mat4(1.0f);

    ImGuizmo::SetOrthographic(false);

    ImGui::Begin("Scene");

    // Left panel - texture list with scrollbar
    ImGui::BeginChild("EntityList", ImVec2(250, 0), true);

    // Render the scene hierarchy as a table tree view
    if (ImGui::BeginTable("SceneHierarchyTable", 2,
                          ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable)) {
      ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_NoHide);
      ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_NoHide);
      ImGui::TableHeadersRow();

      scene.getRoot().children([&](Entity e) { renderEntityHeirarchy(e); });
      ImGui::EndTable();
    }

    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("EntityInspector", ImVec2(0, 0), true);


    if (selectedEntity) {
      renderEntityInspector(selectedEntity);
      if (ImGui::Button("Deselect")) { selectedEntity = {}; }
    }
    ImGui::EndChild();


    ImGui::End();
  }
}  // namespace ren
