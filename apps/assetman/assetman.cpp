#include <ren/core/Application.h>
#include <ren/core/windows/ImGuiWindow.h>
#include <ren/core/Flag.h>
#include <ren/core/ui/EditorUI.h>
#include "imgui.h"
#include "AuthoringSketch.h"
#include <ren/core/ui/EditorUI.h>


static ren::Flag<bool> testAuthoring("test-authoring", false, "Verify the authoring sketch without a window");



static void assetsTab(void) {
  auto& am = ren::getAssetManager();

  ren::eui::ExtendedButton("Scan", ICON_CUBOID);

  if (ren::eui::Button("Re-Scan")) {
    am.scanForImports();
  }

  ImGui::SameLine();

  if (ren::eui::Button("TEMP CLEAR AND RESCAN")) {
    ren::println("Clearing all .import files and rescanning");
    system("find assets -name '*.import' -exec rm '{}' \\;");
    am.scanForImports();
    ren::println("");  // newline
  }

  ImGui::SameLine();
  if (ren::eui::Button("import test")) {
    ren::getAssetManager().importMesh("/Users/nick/dev/Models/sponza.glb");
  }

  ImGui::Separator();

  ImGui::BeginChild("AssetList");
  // a table for each asset.
  am.getSource().enumerate([&](const std::string_view& name) { ImGui::Text("%s", name.data()); });
  ImGui::EndChild();
}


static void importTab(void) {}

int main(int argc, char* argv[]) {
  ren::parseFlags(argc, argv);


  glm::uvec2 res;
  res.x = 640;
  res.y = 480;



  ren::run<ren::LambdaImGuiWindow>("Asset Manager", res, []() {
    // We are inside a Begin/End block. Create a tab region.
    if (ren::eui::BeginTabBar("Asset Manager Tabs")) {


      if (ImGui::BeginTabItem("Authoring")) {
        authoring_sketch::draw();
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Assets")) {
        assetsTab();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Imports")) {
        importTab();
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
  });


  return 0;
}
