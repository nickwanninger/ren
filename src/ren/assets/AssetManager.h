#pragma once

#include <map>
#include <ren/assets/Asset.h>
#include <ren/renderer/shader/ShaderModule.h>
#include <ren/assets/AssetSource.h>
#include <ren/assets/AssetStore.h>
#include <sol/sol.hpp>

namespace ren {

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



  // The AssetManager is responsible for managing asset importing and retrieval.
  // 
  class AssetManager {
   public:
    AssetManager();
    ~AssetManager();
    // Add a filesystem source to the asset manager.
    void addFilesystemSource(const std::string_view &path);
    void addEmbeddedSource(void);

    // Add the sources that all ren programs use: the embedded resources,
    // then the asset directory `root`. The store is in `root`, because the
    // embedded source is read-only. Note: this creates `<root>/store` if it
    // does not exist.
    void addDefaultSources(const std::string_view &root = "assets/");

    // Handles the changes that the sources detected since the last call.
    // Call this one time per frame, from the main thread.
    void tick();

    // Load the bytes of some asset from the asset source. No Caching!
    bool load(const std::string_view &name, std::vector<u8> &out) { return source.load(name, out); }


    AssetStore &getStore() { return store; }
    AssetSource &getSource(void) { return source; }


    // Scan the asset sources for new assets, and import them.
    void scanForImports(void);

    // Import an asset from a file path. This will do the import action,
    // give it an asset id, and write the sidecar.
    Option<AssetID> import(const std::filesystem::path &path);

    // Import a mesh from a file path. implemented in importers/mesh.cpp
    void importMesh(const std::filesystem::path &path);

   private:
    // The source that assets are loaded from.
    ren::MultiAssetSource source;
    ren::AssetStore store;
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
