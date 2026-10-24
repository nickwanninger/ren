#include <ren/core/Application.h>
#include <ren/core/AutoPlugin.h>
#include <ren/core/Systems.h>
#include <ren/core/Components.h>
#include <ren/core/ComponentRegistration.h>

#include <imgui/imgui.h>
#include <cinttypes>




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

      // Components that did not go through ren_register_component have no
      // fields to show, so show only the flecs name and layout.
      auto *ref = comp.try_get<ComponentInfoRef>();
      if (ref == nullptr) {
        ImGui::TextDisabled("%s #%" PRIu64, path(comp).c_str(), comp.raw_id());
        const EcsComponent *comp_desc = comp.try_get<EcsComponent>();
        if (comp_desc) {
          ImGui::SameLine();
          ImGui::TextDisabled(" sz:%d al:%d", comp_desc->size, comp_desc->alignment);
        }
        return;
      }

      const auto &info = *ref->info;
      const char *label = info.name ? info.name : info.typeName;

      // Two components can have fields with the same name, so give each
      // component its own ImGui ID scope.
      ImGui::PushID(static_cast<int>(comp.raw_id()));
      if (ImGui::TreeNode(label)) {
        // A tag has no data and returns null, but it also has no fields.
        auto *data = static_cast<std::byte *>(e.try_get_mut(compId));
        for (const auto &field : info.fields) {
          // TODO: send a document command here instead, for undo and redo.
          if (field.type->inspect(data + field.offset, field)) {
            e.modified(compId);
          }
        }
        ImGui::TreePop();
      }
      ImGui::PopID();
    }
  }

  static void drawEntityHeirarchy(Entity e) {
    char buf[64];
    if (e.name().length() > 0) {
      snprintf(buf, sizeof(buf), "%s #%" PRIu64, e.name().c_str(), e.id());
    } else {
      snprintf(buf, sizeof(buf), "Entity #%" PRIu64, e.id());
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

    if (1) {
      ren::system::onUpdate("ren::editor::EditorInspector").run([&](flecs::iter &it) {
        ImGui::Begin("Inspector");

        auto &world = app.world;


        world.children([](Entity e) { drawEntityHeirarchy(e); });

        // draw the scene heirarchy.

        ImGui::End();
      });
    }

    if (1) {
      ren::system::onUpdate("ren::editor::ComponentInspector").run([](flecs::iter &it) {
        REN_PROFILE_SCOPE("Editor Inspector");
        ImGui::Begin("Component Inspector");


        auto &comps = ren::getRegisteredComponents();
        for (const auto &comp : comps) {
          ImGui::Text("%s   sz: %" PRIu64 ", align: %" PRIu64, comp.typeName, comp.typeSize, comp.typeAlignment);
          for (const auto &field : comp.fields) {
            ImGui::Text("  %s  offset: %u  size:%u", field.name.c_str(), field.offset, field.size);
          }
          ImGui::Separator();
        }

        // ImGui::Separator();

        // while (it.next()) {
        //   auto comps = it.field<EcsComponent>(0);

        //   for (auto i : it) {
        //     ImGui::Text("Component: %s", it.entity(i).path().c_str());
        //     ImGui::Text("  Size: %d bytes", comps[i].size);
        //     ImGui::Text("  Alignment: %d bytes", comps[i].alignment);
        //   }
        // }


        ImGui::End();
      });
    }

    if (1) {
      ren::system::onUpdate("ren::editor::TextureInspector").run([](flecs::iter &it) {
        ImGui::Begin("Texture Inspector");


        auto &texs = ren::Texture::allTextures();
        for (const auto &tex : texs) {
          // ImGui::Text("Texture: %s", tex->getName().c_str());
          // ImGui::Text("  Size: %ux%u", tex->getWidth(), tex->getHeight());

          // ImGui::PushID(&tex);
          if (ImGui::TreeNode(tex->getName().c_str())) {
            ImGui::Text("Vulkan Image: %p", (void *)tex->getImage().get());
            ImGui::Text("Vulkan ImageView: %p", (void *)tex->getImageView());
            ImGui::Text("Vulkan Sampler: %p", (void *)(uintptr_t)tex->getSampler());
            ImVec2 size;
            // fill the size to be 512 pixels max in either dimension, preserving aspect ratio.
            if (tex->getWidth() > tex->getHeight()) {
              size.x = 512.0f;
              size.y = 512.0f * ((float)tex->getHeight() / (float)tex->getWidth());
            } else {
              size.y = 512.0f;
              size.x = 512.0f * ((float)tex->getWidth() / (float)tex->getHeight());
            }
            ImGui::Image(tex->getImGui(), size);
            ImGui::TreePop();
          }
          // ImGui::PopID();
        }



        ImGui::End();
      });
    }
  }

  REN_PLUGIN("EditorInspector", inspectorPlugin);

}  // namespace ren::editor
