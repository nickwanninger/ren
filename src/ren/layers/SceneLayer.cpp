#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_vulkan.h>
#include <imgui_internal.h>


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

    // Entity cube = scene.createEntity("Cube 1");
    // auto mesh = ren::loadGLTF("assets/test/meshes/unit_cube.glb");
    // cube.add<comp::Mesh>(mesh);


    // Entity cube2 = scene.createEntity("Cube 2");
    // cube2.add<comp::Mesh>(mesh);
    // cube2.translation().y = 1.0f;


    ren::PipelineStateDesc desc;


    auto dumpDot = [&](void) {
      fmt::print("digraph G {{\n");
      // define all the nodes.
      fmt::print("  node [shape=box];\n");
      fmt::print("  edge [arrowhead=vee, arrowsize=0.5, fontsize=4];\n");
      fmt::print("  rankdir=LR;\n");

      auto view = scene.getAllWith<comp::Relationship, comp::Name>();


      view.each([&](entt::entity entity, const comp::Relationship &rel, const comp::Name &name) {
        fmt::print("  e{} [label=\"{}\"];\n", (u32)entity, name.name);

        if (rel.parent != entt::null) {
          fmt::print("  e{} -> e{} [label=P];\n", (u32)entity, (u32)rel.parent);
        }
        else {
          fmt::println("  {{ rank=min; e{}; }};", (u32)entity);
        }


        // siblings are the same rank.
        if (rel.nextSibling != entt::null) {
          fmt::println("  e{} -> e{} [label=\"{}.n\"];", (u32)entity, (u32)rel.nextSibling, name.name);
          fmt::println("  {{ rank=same; e{}; e{} }};\n", (u32)entity, (u32)rel.nextSibling);
        }

        if (rel.prevSibling != entt::null) {
          fmt::println("  e{} -> e{} [label=\"{}.p\"];",  (u32)entity, (u32)rel.prevSibling, name.name);
        }

        if (rel.firstChild != entt::null) {
          fmt::print("  e{} -> e{} [style=dotted];\n", (u32)entity, (u32)rel.firstChild);
        }
      });


      fmt::print("}}\n");
    };


    auto a = scene.createEntity("a");
    auto b = scene.createEntity("b");
    auto c = scene.createEntity("c");
    auto d = scene.createEntity("d");

    auto e = scene.createEntity("e");
    auto f = scene.createEntity("f");
    // auto g = scene.createEntity("g");

    a.addChild(b);
    a.addChild(c);
    a.addChild(d);

    d.addChild(e);
    d.addChild(f);

    // a.removeChild(d);
    // a.addChild(d);



    // a.removeChild(c);

    // b.addChild(e);
    // b.addChild(f);

    // c.addChild(g);

    // c.addChild(e);

    dumpDot();

    // // ----------------------

    // exit(0);

    // std::cout << a.serializeRelationships().dump(3) << std::endl;
    // exit(0);


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


  static void drawVec3Control(const std::string &label, glm::vec3 &values, float resetValue = 0.0f,
                              float columnWidth = 100.0f) {
    ImGui::PushID(&values);
    ImGui::Columns(2);

    ImGui::SetColumnWidth(0, columnWidth);
    ImGui::Text("%s", label.c_str());
    ImGui::NextColumn();


    ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0, 0});

    float lineheight = ImGui::GetFrameHeightWithSpacing();
    ImVec2 buttonSize = ImVec2{lineheight + 3, lineheight};


    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.8f, 0.1f, 0.15f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.9f, 0.2f, 0.2f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.8f, 0.1f, 0.15f, 1.0f});
    if (ImGui::Button("X", buttonSize)) { values.x = resetValue; }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::DragFloat("##X", &values.x, 0.1f);
    ImGui::PopItemWidth();
    ImGui::SameLine();



    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.2f, 0.7f, 0.3f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.2f, 0.9f, 0.3f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.2f, 0.7f, 0.3f, 1.0f});
    if (ImGui::Button("Y", buttonSize)) { values.y = resetValue; }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::DragFloat("##Y", &values.y, 0.1f);
    ImGui::PopItemWidth();
    ImGui::SameLine();



    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.1f, 0.25f, 0.8f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.2f, 0.35f, 0.9f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.1f, 0.25f, 0.8f, 1.0f});
    if (ImGui::Button("Z", buttonSize)) { values.z = resetValue; }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::DragFloat("##Z", &values.z, 0.1f);
    ImGui::PopItemWidth();


    ImGui::PopStyleVar();
    ImGui::Columns(1);
    ImGui::PopID();
  }


  void SceneLayer::onImguiRender(float deltaTime) {
    REN_PROFILE_FUNCTION();


    ImGui::Begin("Scene Layer");


    if (ImGui::CollapsingHeader("Scene Information")) {
      drawVec3Control("Position", camera.position, 0.0f, 100.0f);
      drawVec3Control("Rotation", camera.angles, 0.0f, 100.0f);
      drawVec3Control("Velocity", camera.velocity, 0.0f, 100.0f);
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
      drawVec3Control("Position", transform.translation, 0.0f, 100.0f);
      drawVec3Control("Rotation", transform.rotation, 0.0f, 100.0f);
      drawVec3Control("Scale", transform.scale, 1.0f, 100.0f);
      ImGui::PopID();
    }

    ImGui::End();
  }
}  // namespace ren
