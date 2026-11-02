#include <ren/core/Application.h>
#include <ren/core/windows/ImGuiWindow.h>
#include <ren/core/Flag.h>
#include <ren/core/ui/Widgets.h>
#include <ren/core/ui/EditorUI.h>
#include "imgui.h"
#include "AuthoringSketch.h"
#include <ren/core/ui/EditorUI.h>


static ren::Flag<bool> testAuthoring("test-authoring", false, "Verify the authoring sketch without a window");



static void assetsTab(ren::eui::Ui& ui) {
  auto& am = ren::getAssetManager();

  ui.button("Scan", ICON_CUBOID);

  if (ui.button("Re-Scan")) {
    am.scanForImports();
  }

  ui.sameLine();

  if (ui.button("TEMP CLEAR AND RESCAN")) {
    ren::println("Clearing all .import files and rescanning");
    system("find assets -name '*.import' -exec rm '{}' \\;");
    am.scanForImports();
    ren::println("");  // newline
  }

  ui.sameLine();
  if (ui.button("import test")) {
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
    ren::eui::draw([&](ren::eui::Ui& ui) {
      ui.tabBar("Asset Manager Tabs", [&](ren::eui::Ui& ui) {
        ui.tab("Authoring", [&](ren::eui::Ui& ui) { authoring_sketch::draw(ui); });

        ui.tab("Assets", [&](ren::eui::Ui& ui) { assetsTab(ui); });
        ui.tab("Imports", [&](ren::eui::Ui& ui) { importTab(); });
      });
    });
  });


  return 0;
}
