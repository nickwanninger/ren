#include <ren/assets/AssetManager.h>

#include <ren/renderer/Texture.h>
#include <ren/renderer/Shader.h>
#include <imgui/imgui.h>

namespace ren {

  static AssetManager *g_assetManager = nullptr;

  AssetManager &getAssetManager() {
    assert(g_assetManager != nullptr && "AssetManager is not initialized!");
    return *g_assetManager;
  }


  AssetManager::AssetManager(const std::string_view &assetDirectory) {
    this->assetDirectory = assetDirectory;
    fmt::print("AssetManager initialized with directory: {}\n", this->assetDirectory.c_str());
    g_assetManager = this;


    // traverse the asset directory and load all asset information
    fmt::print("Loading asset information from disk...\n");
    for (const auto &entry : std::filesystem::directory_iterator(this->assetDirectory)) {
      if (entry.is_regular_file() && entry.path().extension() == ".json") {
        fmt::print("Found asset information file: {}\n", entry.path().string());
        // // Load the asset information from the file
        // this->load();
        // return;
      }
    }

    load();
  }

  AssetManager::~AssetManager() {
    fmt::print("AssetManager destroyed.\n");
    g_assetManager = nullptr;
  }


  void AssetManager::sync(void) {
    REN_PROFILE_SCOPE("Sync AssetManager databse to disk");
    json info;
    info["version"] = 1;  // Version magic numberkA
    info["assets"] = json::array();
    for (const auto &pair : assetRegistry) {
      const auto &assetInfo = pair.second;
      json assetJson;
      assetJson["id"] = assetInfo.id;
      assetJson["path"] = assetInfo.path;
      assetJson["type"] = assetInfo.type;
      info["assets"].push_back(assetJson);
    }

    // write json to assetDirectory/.assets.ren.json
    std::string infoPath = getAssetPath(".assets.ren.json").c_str();
    std::ofstream file(infoPath);
    file << info.dump(4);
    file.close();
  }

  void AssetManager::load(void) {
    REN_PROFILE_SCOPE("Load AssetManager databse from disk");
    // load the asset information from disk.
    std::string infoPath = getAssetPath(".assets.ren.json").c_str();
    std::ifstream file(infoPath);
    if (!file.is_open()) {
      fmt::print("No asset information file found at {}\n", infoPath);
      return;
    }
    json info;
    file >> info;
    file.close();

    if (info["version"] != 1) {
      fmt::print("Asset information file version mismatch. Expected 1, got {}\n",
                 (u32)info["version"]);
      return;
    }

    fmt::print("Loading asset information from {}\n", infoPath);
    for (const auto &assetJson : info["assets"]) {
      AssetInfo assetInfo;
      assetInfo.id = assetJson["id"];
      assetInfo.path = (std::string)assetJson["path"];
      assetInfo.type = assetJson["type"];

      assetRegistry[assetInfo.id] = assetInfo;  // Store the asset info in the map
    }
  }


  ref<Asset> AssetManager::getAsset(ren::AssetID assetID) {
    REN_PROFILE_SCOPE("Get Asset");
    // First, lookup the asset in the cached asset map.
    if (auto it = loadedAssets.find(assetID); it != loadedAssets.end()) {
      return it->second;  // Return the cached asset if found
    }

    // If it isn't loaded, load the asset if we can.
    if (auto it = assetRegistry.find(assetID); it != assetRegistry.end()) {
      // if the cached asset is not loaded, load it.
      return loadAsset(it->second);
    } else {
      fmt::print("Asset with ID {} is not found in the asset registry\n", (u64)assetID);
    }
    return nullptr;  // Asset not found
  }


  ref<Asset> AssetManager::importAsset(const std::string_view &path, AssetType type) {
    REN_PROFILE_SCOPE("Get Asset");
    // Check if the asset is already loaded
    for (const auto &pair : assetRegistry) {
      if (pair.second.path == path) {
        fmt::print("Asset {} is already imported with ID {}\n", path, (u64)pair.first);
        return getAsset(pair.second.id);
      }
    }


    if (type == AssetType::Unknown) {
      auto extension = [&](const char *ext, AssetType t) {
        // I don't have string_view::ends_with, so we do it manually.
        if (path.size() >= strlen(ext) &&
            path.compare(path.size() - strlen(ext), strlen(ext), ext) == 0) {
          type = t;
          fmt::print("Detected asset type {} for path {}\n", static_cast<int>(t), path);
        }
      };

      // Texture formats we support
      extension(".png", AssetType::Texture);
      extension(".jpg", AssetType::Texture);
      extension(".jpeg", AssetType::Texture);

      // Mesh formats we support
      extension(".glb", AssetType::Mesh);
      extension(".gltf", AssetType::Mesh);
      extension(".obj", AssetType::Mesh);
      extension(".fbx", AssetType::Mesh);

      // Shader formats we support
      extension(".spv", AssetType::Shader);
      extension(".vert", AssetType::Shader);
      extension(".frag", AssetType::Shader);
      extension(".comp", AssetType::Shader);
      extension(".geom", AssetType::Shader);
    }

    // Create a new AssetInfo
    AssetInfo info;
    info.id = UUID();  // Generate a new UUID for the asset
    info.path = path;
    info.type = type;
    assetRegistry[info.id] = info;  // Store the asset info in the map
    sync();                         // sync the asset state!

    return loadAsset(info);
  }


  ref<Asset> AssetManager::loadAsset(const AssetInfo &info) {
    REN_PROFILE_SCOPE("Load Asset");
    std::string path = assetDirectory / info.path;
    ref<Asset> asset = nullptr;

    // Load!
    // TODO: asset loader abstraction
    switch (info.type) {
      case AssetType::Texture: {
        asset = ren::Texture::load(path);
        break;
      }
      case AssetType::Mesh: {
        fmt::print("IMPORTING MESH FROM {} IS NOT IMPLEMENTED\n", path);
        break;
      }
      case AssetType::Shader: {
        fmt::print("Importing shader asset from {}\n", path);
        // Load the shader from the file
        auto stage = Shader::getStageFromFilename(path);
        auto shader = makeRef<Shader>(path, stage);
        if (shader) {
          // Register the shader asset
          shader->setAssetID(info.id);
          loadedAssets[info.id] = shader;  // Cache the loaded shader
          fmt::print("Shader asset {} imported successfully.\n", (u64)info.id);
          return shader;  // Return the loaded shader
        }
        asset = shader;
      }
      case AssetType::Material: {
        fmt::print("IMPORTING MATERIAL FROM {} IS NOT IMPLEMENTED\n", path);
        break;
      }


      case AssetType::Unknown:
      default: {
        fmt::print("Unknown asset type for path {}\n", path);
        break;
      }
    }

    if (asset) {
      asset->setAssetID(info.id);     // Set the asset ID for the asset
      loadedAssets[info.id] = asset;  // Cache the loaded asset
    } else {
      fmt::print("Failed to import asset from path {}\n", path);
    }


    return asset;
  }


  void AssetManager::inspect(void) {
    REN_PROFILE_SCOPE("AssetManager Inspect");
    ImGui::Text("Asset Manager");
    ImGui::Separator();

    if (ImGui::Button("Sync Assets")) {
      sync();
      fmt::print("Assets synced to disk.\n");
    }

    ImGui::Text("Assets Loaded: %zu", loadedAssets.size());
    ImGui::Text("Assets Registered: %zu", assetRegistry.size());

    // List all assets
    for (const auto &pair : assetRegistry) {
      const auto &assetInfo = pair.second;
      ImGui::Text("Asset ID: %llu, Path: %s, Type: %s", (u64)assetInfo.id,
                  assetInfo.path.string().c_str(), json(assetInfo.type).dump().c_str());
    }
  }

}  // namespace ren