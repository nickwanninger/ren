#pragma once

#include <map>
#include <ren/assets/Asset.h>
#include <ren/renderer/Shader.h>
#include <ren/assets/AssetSource.h>


namespace ren {

  struct AssetInfo {
    AssetID id;                  // The ID of the asset.
    std::filesystem::path path;  // The path to the asset file.
    AssetType type;              // The type of the asset.
  };



  // An AssetHandle is a typed wrapper around a u64 integer, which abstracts the
  // resolution of an asset of a given type. Whenever an asset is needed, it can
  // be resolved, but should not be maintained beyond that, to allow the asset
  // manager to manage the lifetimes of assets according to the requirements of
  // the program.
  template <typename T>
    requires(std::is_base_of_v<Asset, T>)
  struct AssetHandle {
    AssetID id;  // Just the ID.

    AssetHandle(AssetID id)
        : id(id) {}
  };



  class AssetManagerBase {
   public:
    virtual ~AssetManagerBase() = default;

    // The core of the asset manager interface is the ability to resolve an
    // asset by its UUID.  If an asset hasn't been used after a period of time,
    // it gets unloaded from memory automatically. Each asset can have its own timeout.
  };




  // AssetManager is an interface for managing assets in the engine.
  // It's mostly used to retrieve assets by their UUIDs.
  class AssetManager {
   public:
    AssetManager();
    ~AssetManager();
    // Add a filesystem source to the asset manager.
    void addFilesystemSource(const std::string_view &path);

    ref<Asset> getAsset(ren::AssetID assetID);
    ref<Asset> getAsset(const std::string_view &name);

    AssetID importAsset(const std::string_view &path, AssetType type);

    // Given an asset's name, return the AssetID which identifies it.
    static AssetID getID(const std::string_view &name);



    // Load the bytes of some asset from the asset source. No Caching!
    bool load(const std::string_view &name, std::vector<u8> &out) { return source.load(name, out); }

    // Load an asset by path and type. If the type is unknown, it will be inferred from the file extension.
    ref<Asset> load(const std::string_view &path, AssetType type = AssetType::Unknown);

   protected:
    ref<Asset> load(const AssetInfo &info);

   private:
    // The source that assets are loaded from.
    ren::MultiAssetSource source;

    std::map<ren::AssetID, AssetInfo> assetRegistry;
    std::map<ren::AssetID, ref<Asset>> loadedAssets;  // Cache of loaded assets
  };

  AssetManager &getAssetManager();

  bool loadAssetBytes(const std::string_view &path, std::vector<u8> &out);

  template <typename T>
  inline ref<T> getAsset(const std::filesystem::path &path) {
    auto &am = getAssetManager();
    auto asset = am.load(path.string(), T::getStaticType());
    if (asset && asset->getType() == T::getStaticType()) {
      return std::static_pointer_cast<T>(asset);
    }
    return nullptr;
  }

}  // namespace ren