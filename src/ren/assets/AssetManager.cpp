#include <ren/assets/AssetManager.h>

#include <ren/renderer/Texture.h>
#include <ren/renderer/Shader.h>
#include <ren/core/Application.h>
#include <imgui/imgui.h>
#include <ren/misc/hash.h>

namespace ren {

  AssetManager &getAssetManager() { return ren::world().get_mut<AssetManager>(); }

  AssetManager::AssetManager() {}

  AssetManager::~AssetManager() { fmt::print("AssetManager destroyed.\n"); }


  void AssetManager::addFilesystemSource(const std::string_view &path) {
    this->source.addFilesystem(path);
  }


  AssetID AssetManager::getID(const std::string_view &name) {
    u64 state = 0;
    // For now, simply hash. Later on we might need to do this centrally.
    // It's unlikely to get a collision, but you never know.
    ren::hashStd(state, name);
    return state;
  }


  ref<Asset> AssetManager::load(const std::string_view &path, AssetType type) {
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


      // Scripts
      extension(".lua", AssetType::Script);
      extension(".fnl", AssetType::Script);
    }

    // Create a new AssetInfo
    AssetInfo info;
    info.id = UUID();  // Generate a new UUID for the asset
    info.path = path;
    info.type = type;
    assetRegistry[info.id] = info;  // Store the asset info in the map
    sync();                         // sync the asset state!

    return load(info);
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
      return load(it->second);
    } else {
      fmt::print("Asset with ID {} is not found in the asset registry\n", (u64)assetID);
    }
    return nullptr;  // Asset not found
  }




  ref<Asset> AssetManager::load(const AssetInfo &info) {
    REN_PROFILE_SCOPE("Load Asset");
    std::string path = info.path;
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


  // void AssetManager::inspect(void) {
  //   REN_PROFILE_SCOPE("AssetManager Inspect");
  //   ImGui::Text("Asset Manager");
  //   ImGui::Separator();
  //   if (ImGui::Button("Sync Assets")) {
  //     sync();
  //     fmt::print("Assets synced to disk.\n");
  //   }
  //   ImGui::Text("Assets Loaded: %zu", loadedAssets.size());
  //   ImGui::Text("Assets Registered: %zu", assetRegistry.size());
  //   // List all assets
  //   for (const auto &pair : assetRegistry) {
  //     const auto &assetInfo = pair.second;
  //     ImGui::Text("Asset ID: %llu, Path: %s, Type: %s", (u64)assetInfo.id,
  //                 assetInfo.path.string().c_str(), json(assetInfo.type).dump().c_str());
  //   }
  // }

  bool loadAssetBytes(const std::string_view &path, std::vector<u8> &out) {
    return getAssetManager().load(path, out);
  }


}  // namespace ren