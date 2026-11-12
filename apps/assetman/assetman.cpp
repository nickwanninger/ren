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
#include "LibraryTest.h"
#include <algorithm>
#include <chrono>
#include <unordered_set>

static ren::Flag<bool> testAuthoring("test-authoring", false, "Verify the authoring sketch without a window");

static ren::Flag<std::string> assetRoot("asset-root", "", "Alias for --assets: configure the application asset source");
static ren::Flag<std::string> importFile("import-file", "", "Image or model path relative to the configured asset sources");
static ren::Flag<bool> importOnly("import-only", false, "Import one file without a window or GPU");
static ren::Flag<std::string> inspectImport("inspect-import", "", "Verify and inspect an import by source ID without a GPU");
static ren::Flag<std::string> loadImport("load-import", "", "Load an import by source ID and upload its bundles to the GPU");
static ren::Flag<bool> verifyImportGPU("verify-import-gpu", false, "Exit after one successful bundle upload test");
static ren::Flag<bool> testImport("test-import", false, "Verify one-file import and bundle loading without a GPU");
static ren::Flag<bool> testLibrary("test-library", false, "Verify asset library scans without a GPU");

// List the files of all asset sources, as they are on disk. The Library tab shows what the
// asset manager knows about them.
static void assetsTab(ren::eui::Ui &ui) {
  auto &manager = ren::getAssetManager();
  ImGui::BeginChild("AssetList");
  manager.getSource().enumerate([&](const std::string_view &name) { ImGui::TextUnformatted(name.data(), name.data() + name.size()); });
  ImGui::EndChild();
}


// Control the asset manager's background tasks, and show the library. The library is read again
// each time it changes, so each import shows as it finishes. Rows that the last change added or
// replaced are highlighted, so an unchanged scan shows no highlight.
static void libraryTab(ren::eui::Ui &ui) {
  auto &manager = ren::getAssetManager();
  auto &library = manager.getLibrary();
  static std::string filter;
  static ren::Option<u64> shownVersion = ren::None;
  static std::unordered_map<ren::AssetID, ren::ref<const ren::AssetEntry>> previous;
  static std::unordered_set<ren::AssetID> changed;


  // Settings of the manager. Each control writes the setting at once.
  bool scanEnabled = manager.isScanEnabled();
  if (ImGui::Checkbox("Timed scans", &scanEnabled)) {
    manager.setScanEnabled(scanEnabled);
  }
  ImGui::SameLine();
  int interval = manager.getScanIntervalMs();
  ImGui::SetNextItemWidth(200);
  if (ImGui::SliderInt("Scan interval (ms)", &interval, 100, 10000, "%d", ImGuiSliderFlags_Logarithmic)) {
    manager.setScanIntervalMs(interval);
  }
  ImGui::SameLine();
  bool autoImport = manager.isAutoImport();
  if (ImGui::Checkbox("Auto-import", &autoImport)) {
    manager.setAutoImport(autoImport);
  }
  ImGui::SameLine();
  bool hashCheck = manager.isHashCheck();
  if (ImGui::Checkbox("Hash check", &hashCheck)) {
    manager.setHashCheck(hashCheck);
  }

  if (ui.button("Scan now")) {
    manager.scheduleScan();
  }
  static const char *activities[] = {"Idle", "Scanning", "Importing"};
  ImGui::SameLine();
  ImGui::Text("Activity: %s  Last task: %.2f ms  Tasks: %llu", activities[static_cast<int>(manager.getActivity())], manager.getLastTaskMilliseconds(),
              static_cast<unsigned long long>(manager.getFinishedTaskCount()));


  // The pending list and the failures. With auto-import off, this is where the app asks the user.
  auto pathOf = [&](ren::AssetID id) {
    auto entry = library.find(id);
    return entry ? entry->path : id.to_string();
  };
  auto pending = manager.getPendingImports();
  auto failed = manager.getFailedImports();
  ImGui::Text("Pending imports: %zu  Failed imports: %zu", pending.size(), failed.size());
  if (!pending.empty()) {
    ImGui::SameLine();
    if (ui.button("Import all")) {
      manager.scheduleImport(pending);
    }
    ImGui::SameLine();
    if (ui.button("Dismiss all")) {
      manager.dismissImports(pending);
    }
    for (auto id : pending) {
      ImGui::BulletText("%s", pathOf(id).c_str());
    }
  }
  if (!failed.empty() && ImGui::TreeNode("Failures")) {
    for (const auto &[id, error] : failed) {
      ImGui::TextWrapped("%s: %s", pathOf(id).c_str(), error.c_str());
    }
    ImGui::TreePop();
  }
  ImGui::Separator();


  // Read the library again when it changed. An entry object that differs from the last read is
  // new or replaced.
  if (!shownVersion || shownVersion.unwrap() != library.getVersion()) {
    shownVersion = ren::Some(library.getVersion());
    std::unordered_map<ren::AssetID, ren::ref<const ren::AssetEntry>> current;
    changed.clear();
    library.forEach([&](const ren::ref<const ren::AssetEntry> &entry) {
      current[entry->id] = entry;
      auto it = previous.find(entry->id);
      if (it == previous.end() || it->second != entry) {
        changed.insert(entry->id);
      }
    });
    previous = std::move(current);
  }

  ImGui::InputText("Filter", &filter);
  ImGui::Text("Entries: %zu  Changed by last update: %zu", previous.size(), changed.size());

  // Top rows are Source and Native entries. Derived entries are children of their Source.
  // Sort by path, so that rows do not move between frames.
  std::vector<ren::ref<const ren::AssetEntry>> rows;
  std::unordered_map<ren::AssetID, std::vector<ren::ref<const ren::AssetEntry>>> outputs;
  for (const auto &[id, entry] : previous) {
    if (entry->kind == ren::AssetKind::Derived) {
      outputs[entry->source].push_back(entry);
    } else if (filter.empty() || entry->path.find(filter) != std::string::npos) {
      rows.push_back(entry);
    }
  }
  auto byPath = [](const auto &a, const auto &b) { return a->path < b->path; };
  std::sort(rows.begin(), rows.end(), byPath);
  for (auto &[id, children] : outputs) {
    std::sort(children.begin(), children.end(), [](const auto &a, const auto &b) { return a->name < b->name; });
  }

  static const char *kinds[] = {"Source", "Native", "Derived"};
  static const char *states[] = {"", "Not imported", "Imported", "Stale"};

  // Draw one row. Returns true if the row is an open tree node, and the caller must call TreePop().
  auto drawRow = [&](const ren::AssetEntry &entry, bool hasChildren) {
    ImGui::TableNextRow();
    if (changed.contains(entry.id)) {
      ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(200, 160, 40, 80));
    }

    ImGui::TableNextColumn();
    ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_SpanFullWidth;
    if (!hasChildren) {
      nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    ImGui::PushID(entry.id.to_string().c_str());
    bool open = ImGui::TreeNodeEx(kinds[static_cast<int>(entry.kind)], nodeFlags);
    ImGui::PopID();

    // A Source can make assets of several types, so it shows none.
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(entry.kind == ren::AssetKind::Source ? "-" : json(entry.type).get<std::string>().c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(entry.name.c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(entry.id.to_string().c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(states[static_cast<int>(entry.importState)]);
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(entry.path.c_str());
    return open && hasChildren;
  };

  ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
  if (!ImGui::BeginTable("Library", 6, flags)) {
    return;
  }
  ImGui::TableSetupScrollFreeze(0, 1);
  ImGui::TableSetupColumn("Kind");
  ImGui::TableSetupColumn("Type");
  ImGui::TableSetupColumn("Name");
  ImGui::TableSetupColumn("ID");
  ImGui::TableSetupColumn("Import");
  ImGui::TableSetupColumn("Path");
  ImGui::TableHeadersRow();
  for (const auto &entry : rows) {
    auto it = outputs.find(entry->id);
    if (!drawRow(*entry, it != outputs.end())) {
      continue;
    }
    for (const auto &child : it->second) {
      drawRow(*child, false);
    }
    ImGui::TreePop();
  }
  ImGui::EndTable();
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
                               ui.tab("Library", [&](ren::eui::Ui &ui) { libraryTab(ui); });
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
    if (testLibrary.get()) {
      return runLibraryTests();
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
