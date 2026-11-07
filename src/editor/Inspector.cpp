#include <ren/core/Application.h>
#include <ren/core/AutoPlugin.h>
#include <ren/core/Systems.h>

#include <imgui/imgui.h>
#include <cinttypes>




namespace ren::editor {


  static void inspectorPlugin(ren::Application &app) {

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
