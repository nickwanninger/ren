#include <ren/core/Application.h>
#include <ren/core/AutoPlugin.h>
#include <ren/core/Systems.h>
#include <ren/core/Components.h>
#include <ren/core/ComponentRegistration.h>

#include <imgui/imgui.h>


namespace ren::editor {


  auto path(flecs::entity e) { return e.path(".", ""); }

  static void drawComponent(Entity e, flecs::id compId) {
    // ImGui::Text("Component: %lx", comp);
    e.world().component<ren::PositionComponent>();

    if (compId.is_pair()) {
      auto rel = compId.first();
      auto obj = compId.second();
      ImGui::Text("(%s, %s)", rel.path(".", "").c_str(), obj.path(".", "").c_str());
    } else {
      auto comp = compId.entity();

      // auto type = comp.type();
      ImGui::Text("%s #%lu", compId.entity().path(".", "").c_str(), comp.raw_id());
      const EcsComponent *comp_desc = comp.try_get<EcsComponent>();
      // const void *comp_data = e.try_get(comp);
      if (comp_desc) {
        ImGui::SameLine();
        ImGui::Text(" sz:%lu al:%lu", comp_desc->size, comp_desc->alignment);
      }

      // ImGui::Text("  Size: %lu bytes", comp_data.size);
    }
  }

  static void drawEntityHeirarchy(Entity e) {
    char buf[64];
    if (e.name().length() > 0) {
      snprintf(buf, sizeof(buf), "%s #%llu", e.name().c_str(), e.id());
    } else {
      snprintf(buf, sizeof(buf), "Entity #%llu", e.id());
    }
    if (ImGui::TreeNode(buf)) {
      // iterate over the components and draw them.
      e.each([&](flecs::id comp) -> void { drawComponent(e, comp); });

      ImGui::Separator();

      e.children([&](Entity child) { drawEntityHeirarchy(child); });
      ImGui::TreePop();
    }
  }

  static void inspectorPlugin(ren::Application &app) {
    ren::system::onUpdate("ren::editor::EditorInspector").run([&](flecs::iter &it) {
      ImGui::Begin("Inspector");

      auto &world = app.world;


      world.children([](Entity e) { drawEntityHeirarchy(e); });

      // draw the scene heirarchy.

      ImGui::End();
    });

    ren::system::onUpdate<EcsComponent>("ren::editor::ComponentInspector").run([](flecs::iter &it) {
      ImGui::Begin("Component Inspector");


      auto &comps = ren::getRegisteredComponents();
      for (const auto &comp : comps) {
        ImGui::Text("Registered Component: %s", comp.typeName);
        ImGui::Text("  Size: %lu bytes", comp.typeSize);
        ImGui::Text("  Alignment: %lu bytes", comp.typeAlignment);
      }

      ImGui::Separator();

      while (it.next()) {
        auto comps = it.field<EcsComponent>(0);

        for (auto i : it) {
          ImGui::Text("Component: %s", it.entity(i).path().c_str());
          ImGui::Text("  Size: %lu bytes", comps[i].size);
          ImGui::Text("  Alignment: %lu bytes", comps[i].alignment);
        }
      }


      ImGui::End();
    });
  }

  REN_PLUGIN("EditorInspector", inspectorPlugin);

}  // namespace ren::editor