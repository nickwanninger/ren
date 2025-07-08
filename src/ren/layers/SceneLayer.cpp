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

namespace ren {

  SceneLayer::SceneLayer(Application &app)
      : Layer(app, "ImGui") {}


  void SceneLayer::onAttach(void) {
    REN_PROFILE_FUNCTION();
    fmt::print("Scene Layer Attached\n");


    this->meshScene = MeshScene::loadGLTF("assets/test/meshes/simple_scene.glb");
    // this->meshScene = MeshScene::loadGLTF("assets/test/meshes/unit_cube.glb");


    this->meshScene->instantiate(scene);


    // MeshScene::loadGLTF("assets/test/meshes/cat.glb")->instantiate(scene);


    auto dumpDot = [&](void) {
      fmt::print("digraph G {{\n");
      // define all the nodes.
      fmt::print("  node [shape=box];\n");
      fmt::print("  edge [arrowhead=vee, arrowsize=0.5, fontsize=4];\n");
      fmt::print("  rankdir=LR;\n");

      auto view = scene.getAllWith<comp::Relationship, comp::Name>();


      view.each([&](entt::entity entity, const comp::Relationship &rel, const comp::Name &name) {
        fmt::print("  e{} [label=\"{}\"];\n", (u32)entity, name.name);

        if (rel.parent != UUID::null) {
          fmt::print("  e{} -> e{} [label=P];\n", (u32)entity, (u32)rel.parent);
        } else {
          fmt::println("  {{ rank=min; e{}; }};", (u32)entity);
        }


        // siblings are the same rank.
        if (rel.nextSibling != UUID::null) {
          fmt::println("  e{} -> e{} [label=\"{}.n\"];", (u32)entity, (u32)rel.nextSibling,
                       name.name);
          fmt::println("  {{ rank=same; e{}; e{} }};\n", (u32)entity, (u32)rel.nextSibling);
        }

        if (rel.prevSibling != UUID::null) {
          fmt::println("  e{} -> e{} [label=\"{}.p\"];", (u32)entity, (u32)rel.prevSibling,
                       name.name);
        }

        if (rel.firstChild != UUID::null) {
          fmt::print("  e{} -> e{} [style=dotted];\n", (u32)entity, (u32)rel.firstChild);
        }
      });


      fmt::print("}}\n");
    };


    // auto a = scene.createEntity("a");
    // auto b = scene.createEntity("b");
    // auto c = scene.createEntity("c");
    // auto d = scene.createEntity("d");

    // auto e = scene.createEntity("e");
    // auto f = scene.createEntity("f");
    // a.addChild(b);
    // a.addChild(c);
    // a.addChild(d);
    // d.addChild(e);
    // d.addChild(f);
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

    auto eid = entity.getUUID();
    ImGui::PushID((u64)eid);

    if (ImGui::TreeNode(entity.getName().c_str())) {
      ren::renderEntityInspector(entity);

      entity.eachChild([this](Entity child) {
        // fmt::print("Child: {}\n", child.getName());
        renderEntityHeirarchy(child);
      });

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
    // ImGuizmo::SetDrawlist();
    // int windowWidth, windowHeight;
    // SDL_GetWindowSize(app.getWindow(), &windowWidth, &windowHeight);




    ImGui::Begin("Scene Layer");

    if (ImGui::CollapsingHeader("Scene Information")) {
      drawVec3Control("Position", camera.position, 0.0f, 100.0f);
      drawVec3Control("Rotation", camera.angles, 0.0f, 100.0f);
      drawVec3Control("Velocity", camera.velocity, 0.0f, 100.0f);
    }

    if (ImGui::CollapsingHeader("Mesh Scene")) { meshScene->onImguiRender(); }

    ImGui::Separator();

    // render the root nodes, and their children.
    auto view = scene.getAllWith<comp::Relationship>();
    for (auto [eid, rel] : view.each()) {
      if (rel.parent == UUID::null) {
        Entity e(eid, &this->scene);
        renderEntityHeirarchy(e);
      }
    }



    ImGui::End();
  }
}  // namespace ren
