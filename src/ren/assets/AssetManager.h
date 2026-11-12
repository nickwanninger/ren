#pragma once

#include <map>
#include <ren/assets/Asset.h>
#include <ren/renderer/shader/ShaderModule.h>
#include <ren/assets/AssetLibrary.h>
#include <ren/assets/AssetSource.h>
#include <ren/assets/AssetStore.h>
#include <sol/sol.hpp>

namespace ren {


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

    // The assets that the sources hold. Call getLibrary().scan() to update it.
    AssetLibrary &getLibrary() { return library; }
    AssetSource &getSource(void) { return source; }


    // Import supported files through importFile, excluding generated bundles and sidecars.
    // Returns imported paths and per-file errors. Each call generates fresh outputs.
    json scanForImports(void);

    // Import one source file from the configured sources. Use its sidecar importer or infer one from its extension.
    // Throws if the importer, settings, source, or writes are invalid.
    json importFile(const std::filesystem::path &path);

   private:
    // The source that assets are loaded from.
    ren::MultiAssetSource source;
    ren::AssetStore store;
    ren::AssetLibrary library;
  };

  AssetManager &getAssetManager();

  bool loadAssetBytes(const std::string_view &path, std::vector<u8> &out);

}  // namespace ren
