#include <ren/core/Application.h>
#include <ren/core/windows/ImGuiWindow.h>
#include <ren/core/Flag.h>
#include <ren/core/ui/Widgets.h>
#include <ren/core/ui/EditorUI.h>
#include <ren/assets/DerivedAsset.h>
#include <ren/assets/MeshAsset.h>
#include <ren/assets/TextureAsset.h>
#include <ren/renderer/Texture.h>
#include <ren/assets/Mesh.h>
#include <imgui/misc/cpp/imgui_stdlib.h>
#include "AuthoringSketch.h"
#include "ImportTest.h"

static ren::Flag<bool> testAuthoring("test-authoring", false, "Verify the authoring sketch without a window");

static ren::Flag<std::string> assetRoot("asset-root", "", "Alias for --assets: configure the application asset source");
static ren::Flag<std::string> importFile("import-file", "", "Image or model path relative to the configured asset sources");
static ren::Flag<bool> importOnly("import-only", false, "Import one file without a window or GPU");
static ren::Flag<std::string> inspectImport("inspect-import", "", "Verify and inspect an import by source ID without a GPU");
static ren::Flag<std::string> loadImport("load-import", "", "Load an import by source ID and upload its bundles to the GPU");
static ren::Flag<bool> verifyImportGPU("verify-import-gpu", false, "Exit after one successful bundle upload test");
static ren::Flag<bool> testImport("test-import", false, "Verify one-file import and bundle loading without a GPU");

static void assetsTab(ren::eui::Ui &ui) {
  auto &manager = ren::getAssetManager();
  static json lastScan;
  static std::string scanError;

  if (ui.button("Import source files")) {
    scanError.clear();
    lastScan = {};
    try {
      lastScan = manager.scanForImports();
    } catch (const std::exception &error) {
      scanError = error.what();
    }
  }
  ImGui::TextUnformatted("Imports all supported source files and generates fresh bundles.");

  if (!scanError.empty()) {
    ImGui::TextWrapped("%s", scanError.c_str());
  }
  if (lastScan.is_object()) {
    ImGui::Text("Imported: %zu  Failed: %zu", lastScan.at("imported").size(), lastScan.at("errors").size());
    for (const auto &failure : lastScan.at("errors")) {
      ImGui::TextWrapped("%s: %s", failure.at("path").get<std::string>().c_str(), failure.at("error").get<std::string>().c_str());
    }
  }

  ImGui::Separator();
  ImGui::BeginChild("AssetList");
  manager.getSource().enumerate([&](const std::string_view &name) { ImGui::TextUnformatted(name.data(), name.data() + name.size()); });
  ImGui::EndChild();
}


namespace {

  // One derived asset after GPU upload. Exactly one of mesh and texture is set.
  struct LoadedResource {
    std::string name;
    ren::ref<ren::Mesh> mesh;
    ren::ref<ren::Texture> texture;
  };


  // Upload one bundle, then make sure the GPU objects match it. Throws if they do not.
  LoadedResource uploadAsset(ren::Asset &asset) {
    LoadedResource resource;
    resource.name = asset.name;

    if (const auto *mesh = asset.as<ren::MeshAsset>()) {
      resource.mesh = ren::make<ren::Mesh>(asset.name, mesh->vertices, mesh->indices, mesh->bvh);
      const auto *bvh = resource.mesh->getBVH();
      if (!bvh || bvh->getNodeCount() != mesh->bvh.nodes.size() || resource.mesh->getVertexCount() != mesh->vertices.size() ||
          resource.mesh->getIndexCount() != mesh->indices.size()) {
        throw std::runtime_error("Mesh GPU upload does not match bundle: " + asset.name);
      }
      return resource;
    }

    auto *found = asset.as<ren::TextureAsset>();
    if (!found) {
      throw std::runtime_error("Cannot upload this asset type: " + asset.name);
    }
    auto &texture = *found;
    VkFormat format = texture.pixelFormat == ren::PixelFormat::RGBA8_SRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    resource.texture = ren::Texture::create(asset.name, texture.width, texture.height, texture.pixels.data(), format);
    if (resource.texture->getWidth() != texture.width || resource.texture->getHeight() != texture.height) {
      throw std::runtime_error("Texture GPU upload does not match bundle: " + asset.name);
    }
    return resource;
  }


  void drawResource(const LoadedResource &resource) {
    if (resource.mesh) {
      ImGui::Text("Vertices: %u  Indices: %u  BVH nodes: %u", resource.mesh->getVertexCount(), resource.mesh->getIndexCount(),
                  resource.mesh->getBVH()->getNodeCount());
      return;
    }

    ImGui::Text("Pixels: %u x %u", resource.texture->getWidth(), resource.texture->getHeight());
    float width = std::min(256.0f, float(resource.texture->getWidth()));
    float height = width * resource.texture->getHeight() / resource.texture->getWidth();
    ImGui::Image((ImTextureID)resource.texture->getImGui(), ImVec2(width, height));
  }


  // The Imports tab: import one file, read its record, and upload its bundles to the GPU.
  struct ImportPanel {
    std::string path = importFile.get();
    std::string sourceID = loadImport.get();
    json record;
    std::string error;
    std::map<std::string, LoadedResource> loaded;

    void importOne() {
      record = ren::getAssetManager().importFile(path);
      sourceID = record.at("sourceAsset").get<std::string>();
    }


    void readRecord() { record = ren::readImportRecord(ren::getAssetManager().getSource(), ren::parseAssetID(sourceID)); }


    void loadResources() {
      readRecord();
      auto assets = ren::readImportedAssets(ren::getAssetManager().getSource(), record);
      for (auto &asset : assets) {
        auto id = asset->id.to_string();
        if (!loaded.contains(id)) {
          loaded.emplace(id, uploadAsset(*asset));
        }
      }
      ren::println("Loaded {} bundles from import {} and uploaded them to the GPU", assets.size(), sourceID);
    }


    // Run a button action, and show its error in the panel.
    template <typename Fn>
    void action(Fn &&fn) {
      error.clear();
      try {
        fn();
      } catch (const std::exception &failure) {
        error = failure.what();
      }
    }


    void drawOutput(const json &output) {
      std::string id = output.at("id");
      ImGui::PushID(id.c_str());
      if (ImGui::TreeNode(output.at("name").get<std::string>().c_str())) {
        ImGui::Text("%s  %s", output.at("type").get<std::string>().c_str(), id.c_str());
        ImGui::TextWrapped("%s", output.at("path").get<std::string>().c_str());
        auto it = loaded.find(id);
        if (it != loaded.end()) {
          drawResource(it->second);
        }
        ImGui::TreePop();
      }
      ImGui::PopID();
    }


    void draw() {
      ImGui::TextUnformatted("Paths are relative to the configured asset sources (--assets).");
      ImGui::InputText("Source file", &path);
      if (ImGui::Button("Import one file")) {
        action([&]() { importOne(); });
      }

      ImGui::InputText("Source ID", &sourceID);
      if (ImGui::Button("Read import record")) {
        action([&]() { readRecord(); });
      }
      ImGui::SameLine();
      if (ImGui::Button("Load bundles to GPU")) {
        action([&]() { loadResources(); });
      }

      if (!error.empty()) {
        ImGui::TextWrapped("%s", error.c_str());
      }
      if (!record.is_object()) {
        return;
      }

      for (const auto &warning : record.at("warnings")) {
        ImGui::TextWrapped("%s", warning.get<std::string>().c_str());
      }
      ImGui::Separator();
      for (const auto &output : record.at("derivedAssets")) {
        drawOutput(output);
      }
    }
  };



  //=========================
  // Run modes
  //=========================

  // Import one file and print its record. Needs no window or GPU.
  int runImportOnly(const std::string &root) {
    if (importFile.get().empty()) {
      throw std::runtime_error("--import-only requires --import-file");
    }

    ren::AssetManager manager;
    manager.addDefaultSources(root);
    auto record = manager.importFile(importFile.get());
    ren::println("{}", record.dump(2));
    return 0;
  }


  // Read and verify the bundles of one import. Needs no source files, window, or GPU.
  int runInspect(const std::string &root) {
    auto id = ren::parseAssetID(inspectImport.get());

    ren::AssetManager manager;
    manager.addDefaultSources(root);
    auto record = ren::readImportRecord(manager.getSource(), id);
    auto assets = ren::readImportedAssets(manager.getSource(), record);
    for (const auto &asset : assets) {
      ren::println("{} {} {}", asset->id.to_string(), json(asset->getType()).get<std::string>(), asset->name);
    }
    ren::println("Verified {} bundles without source files or a GPU", assets.size());
    return 0;
  }


  int runWindow(void) {
    if (verifyImportGPU.get() && loadImport.get().empty() && importFile.get().empty()) {
      throw std::runtime_error("--verify-import-gpu requires --load-import or --import-file");
    }

    // The window draws the panel through this pointer. The panel is made after the Application, so it
    // releases its GPU resources before the Application destroys the renderer.
    ImportPanel *panel = nullptr;
    ren::Application app("Asset Manager", {960, 720}, ren::makeBox<ren::LambdaImGuiWindow>([&]() {
                           ren::eui::draw([&](ren::eui::Ui &ui) {
                             ui.tabBar("Asset Manager Tabs", [&](ren::eui::Ui &ui) {
                               ui.tab("Imports", [&](ren::eui::Ui &ui) { panel->draw(); });
                               ui.tab("Assets", [&](ren::eui::Ui &ui) { assetsTab(ui); });
                               ui.tab("Authoring", [&](ren::eui::Ui &ui) { authoring_sketch::draw(ui); });
                             });
                           });
                         }));
    ImportPanel state;
    panel = &state;

    if (!importFile.get().empty()) {
      state.importOne();
    }
    if (!loadImport.get().empty() || verifyImportGPU.get()) {
      state.loadResources();
    }

    ren::Application::RunOptions options;
    options.maxFrames = verifyImportGPU.get() ? 1 : 0;
    app.run(options);
    return 0;
  }

}  // namespace


int main(int argc, char *argv[]) {
  REN_PROFILE_BEGIN("assetman");
  ren::parseFlags(argc, argv);

  try {
    // --asset-root sets the --assets flag of the library, so the Application uses the same root.
    auto *sourceFlag = dynamic_cast<ren::Flag<std::string> *>(ren::FlagRegistry::getRegistry().at("assets"));
    if (!sourceFlag) {
      throw std::runtime_error("Application asset source flag is unavailable");
    }
    if (!assetRoot.get().empty()) {
      sourceFlag->set(assetRoot.get());
    }

    if (testImport.get()) {
      return runImportTests();
    }
    if (importOnly.get()) {
      return runImportOnly(sourceFlag->get());
    }
    if (!inspectImport.get().empty()) {
      return runInspect(sourceFlag->get());
    }
    return runWindow();
  } catch (const std::exception &error) {
    ren::errln("{}", error.what());
    return 1;
  }
}
