#pragma once
#include <ren/types.h>
#include <ren/assets/AssetSource.h>


namespace ren {


  // An asset store is the manager which handles importing, and then
  // in the future, loading of assets. When an asset is imported, it
  // is given a unique ID, and converted into the correct format for
  // the engine to use.  It then stores a json file (name.asset) which
  // contains the metadata for the asset. This is the asset which you
  // will reference and use in the engine, and the asset store will
  // handle loading the actual data from the compiled asset blob
  // files.
  // The compiled asset blob files are stored in the store/ directory
  // under the asset path. Lets imagine a mesh asset, cube.glb which
  // has two textures, an albedo and a normal map. The AssetStore will
  // need to import this (either permanently, or temporarilly in
  // memory).  It will need an import name - in this case, cube - and
  // will store the important data in a cube.asset file. The asset is
  // then several files, such as the triangle data, as well as the
  // texture data in two other files.k
  //
  // The store itself is a content addressable store, meaning the data
  // is stored in a way that it can be retrieved by its hash. The store
  // looks like this:
  // <ASSET_STORE_DIR>/
  //   <hash1>/
  //     data.blob
  //     meta.json
  //   <hash2>/
  //     data.blob
  //     meta.json
  //
  // Importantly, the store should almost always be able to be rebuilt
  // from the source assets, so each .asset file has a reference to its
  // set of source files.
  // So the cube.asset file will point back to the cube.glb file, and
  // the asset store can be verified/validated by checking a few things:
  //   1. the source file(s) exist
  //   2. the source file(s) have the expected mtime/ctime (rsync semantics) (future work)
  //   3. the source file(s) have the same hash as when they were imported
  // Thus, the asset file must store the references to the sources
  // along side this information.
  // The store actually accesses the filesystem through an
  // AssetSource, which abstracts the filesystem for when the engine
  // runs in a different environment such as in production with asset
  // packs and such.
  //
  // Because the asset store is a content addressable store, it must
  // also be garbage collected.  This is done by checking the asset
  // files and removing any files that are not referenced by any asset
  // file.

  static constexpr const char *ASSET_STORE_DIR = "store";

  class AssetStore {
   public:
    AssetStore(AssetSource &source)
        : source(source) {
      reinit();
    }


    inline void reinit(void) {
      // attempt to create the store directory.
      // if it returns true, then we can write to the store (it has a mutable backing)
      canImport = source.mkdir(ASSET_STORE_DIR);
    }

    ren::Option<json> loadMeta(const std::string_view &hash);
    /**
     * @brief Writes `data` and `meta` to the store.
     * @param name  Optional suffix for the key, as in Nix "<hash>-name-version".
     *              See `hash` for the rules.
     * @return The store key (the directory name under store/), or None if
     *         `name` is not valid or a write failed.
     */
    ren::Option<std::string> storeBlob(const ByteSpan data, const json &meta, std::string_view name = "blob");

    /**
     * @brief Computes the store key of an entry. All store keys must come from this function.
     *
     * The key is "<hash>-<name>", where <hash> is SHA-256 of the fingerprint
     * "blob:sha256:<hex SHA-256 of data followed by metaJson>:<name>",
     * XOR-folded to 20 bytes (Nix `compressHash`), in Nix base32 (32
     * characters). Characters that are not legal in file names are replaced
     * with '_' in `name`.
     *
     * @param metaJson  The exact bytes that are written to meta.json.
     */
    static std::string hash(ByteSpan data, std::string_view metaJson, std::string_view name);


    enum class VerifyResult {
      Ok,        ///< The key matches data.blob and meta.json.
      BadKey,    ///< The key does not have the form "<hash>-<name>".
      Missing,   ///< data.blob or meta.json cannot be read.
      Mismatch,  ///< The contents do not give this key.
    };

    /// @brief Recomputes the key of the store entry `key` from its files, and compares it.
    VerifyResult verify(std::string_view key);

    /// @brief Calls `callback` with the name of each directory under store/.
    /// The names are not checked: use `verify` to find names that are not keys.
    /// @return `false` if the store cannot be listed.
    bool listKeys(std::function<void(std::string_view)> callback);

   private:
    bool canImport = false;
    AssetSource &source;
  };



}  // namespace ren
