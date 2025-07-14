#pragma once


#include <ren/assets/Asset.h>
#include <ren/renderer/Shader.h>

namespace ren {

  struct AssetInfo {
    AssetID id;                  // The ID of the asset.
    std::filesystem::path path;  // The path to the asset file.
    AssetType type;              // The type of the asset.
  };

  // AssetManager is an interface for managing assets in the engine.
  // It's mostly used to retrieve assets by their UUIDs.
  class AssetManager {
   public:
    AssetManager(const std::string_view &assetDirectory = "assets");
    ~AssetManager();



    // Get an asset by its asset ID. If the asset has not been imported into ren
    // yet, return nullptr.
    ref<Asset> getAsset(ren::AssetID assetID);
    // Import an asset from a file path. If the asset already is imported, this
    // will return the existing asset. Otherwise it will register a new one
    // with the system. If Unknown is the type passed, it will try to deduct it based
    // on the file extension.
    ref<Asset> importAsset(const std::string_view &path, AssetType type = AssetType::Unknown);

    // Render the asset manager using ImGui
    void inspect(void);

   private:
    // sync the in-memory asset cache with the disk.
    void sync(void);
    // Force the asset manager to load the asset information from disk.
    void load(void);

    std::filesystem::path getAssetPath(const std::string_view &assetPath) const {
      return this->assetDirectory / assetPath;
    }


    u64 getAssetTimestamp(const std::string_view &assetPath) const {
      auto path = getAssetPath(assetPath);
      if (std::filesystem::exists(path)) {
        return std::filesystem::last_write_time(path).time_since_epoch().count();
      }
      return 0;  // Return 0 if the file does not exist
    }


    ref<Asset> loadAsset(const AssetInfo &info);




   private:
    std::filesystem::path assetDirectory;


    std::unordered_map<ren::AssetID, AssetInfo> assetRegistry;
    std::unordered_map<ren::AssetID, ref<Asset>> loadedAssets;  // Cache of loaded assets
  };

  AssetManager &getAssetManager();

  inline ref<Asset> getAsset(ren::AssetID assetID) { return getAssetManager().getAsset(assetID); }
  inline ref<Asset> importAsset(const std::string_view &path, AssetType type = AssetType::Unknown) {
    return getAssetManager().importAsset(path, type);
  }


  template <typename T>
  inline ref<T> getAsset(const std::filesystem::path &path) {
    auto asset = getAssetManager().importAsset(path.string(), T::getStaticType());
    if (asset && asset->getType() == T::getStaticType()) {
      return std::static_pointer_cast<T>(asset);
    }
    return nullptr;
  }

}  // namespace ren