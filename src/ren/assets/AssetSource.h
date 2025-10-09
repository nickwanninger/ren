#pragma once

#include <ren/types.h>
#include <vector>
#include <string>

#include <functional>
#include <filesystem>

namespace ren {

  /**
   * @class AssetSource
   * @brief Generic byte‑stream loader for named resources.
   *
   * `AssetSource` defines a single virtual interface that abstracts how
   * raw asset data is fetched.  A *name* (usually a path or key) is used
   * to request the contents of an asset as a vector of bytes.  The
   * concrete implementation decides where the data comes from.
   *
   * For example, (not implemented yet)
   * - `FilesystemAssetSource`: reads from a file system directory.
   * - `TarballAssetSource`:    reads from an on‑disk or in‑memory tarball.
   * - `NetworkAssetSource`:    fetches via HTTP, FTP, or any other network protocol.
   * - `MultiAssetSource`:      Tries to load from several AssetSource instances
   *                           before failing
   *
   * The interface purposefully performs **no caching**; it simply
   * translates a name into a byte array.  Any memoization or LRU
   * eviction must be handled by the caller or a higher‑level cache.
   *
   */
  class AssetSource {
   public:
    virtual ~AssetSource() = default;

    /**
     * @brief Loads the raw byte contents of an asset identified by `name`
     * @param name  The logical name (path) of the asset to load.
     * @param out   The destination for the loaded data.
     * @return     `true` if the asset was loaded, `false` otherwise.
     */
    virtual bool load(const std::string_view &name, std::vector<u8> &out) = 0;


    /**
     * @brief Enumerate every key/name in the store.
     *
     * Each key in the asset store is returned through the callback, and
     * directories should be traversed in a sane manner. (i.e., seeing the keys
     * a/b, c/d, a/e is an invalid traversal. It should be a/b, a/e, c/d).
     * Thus, this traversal is essentially a flattened recursive walk of the
     * directory structure.
     *
     * @param callback  Function object to be called for every key.  The
     *                  callback must not throw; if it does, the
     *                  enumeration stops and the exception is propagated
     *                  to the caller.  Returning from the callback
     *                  (i.e. not throwing) signals normal progress.
     * @return `true` if the enumeration completed successfully,
     *         `false` otherwise (e.g. the store is empty, I/O errors,
     *         or the derived class does not support enumeration).
     */
    virtual bool enumerate(std::function<void(const std::string_view &)> callback) { return false; }
  };




  // ------ //
  class FilesystemAssetSource final : public AssetSource {
   public:
    FilesystemAssetSource(const std::filesystem::path &root);

    ~FilesystemAssetSource() override = default;
    bool load(const std::string_view &name, std::vector<u8> &out) override;
    bool enumerate(std::function<void(const std::string_view &)> callback) override;



   private:
    std::filesystem::path m_root;  ///< Directory that serves as the root.
  };


  // EmbeddedAssetSource - for assets compiled into the binary
  class EmbeddedAssetSource final : public AssetSource {
   public:
    EmbeddedAssetSource() = default;
    ~EmbeddedAssetSource() override = default;
    bool load(const std::string_view &name, std::vector<u8> &out) override;
    bool enumerate(std::function<void(const std::string_view &)> callback) override;

   private:
    // Nothing! This operates on global data compiled into the binary.
  };

  // ------ //


  /**
   * @class  MultiAssetSource
   * @brief  Aggregates several underlying {@link AssetSource}s.
   *
   * `MultiAssetSource` is a composite `AssetSource` that keeps an ordered
   * collection of child stores.  When a request is made via
   * {@link load} or {@link enumerate}, the call is forwarded to the
   * stores **in the order they were inserted** – the first store that
   * succeeds takes precedence.
   *
   * @note
   *   * The `stores` vector owns the child `AssetSource` objects via
   *     `std::unique_ptr`.  Therefore the lifetime of a child store
   *     is tied to the `MultiAssetSource` instance – you cannot
   *     remove a store after it has been inserted.
   *   * `enumerate` iterates over all child stores in order and
   *     invokes the supplied callback for every key found.  If a key
   *     appears in multiple stores, the callback will be invoked
   *     multiple times – once per store that contains it.  The
   *     caller can filter duplicates if desired.
   *
   * @see AssetSource
   * @see emplace
   */
  class MultiAssetSource : public AssetSource {
   public:
    ~MultiAssetSource() override = default;
    bool load(const std::string_view &name, std::vector<u8> &out) override;
    bool enumerate(std::function<void(const std::string_view &)> callback) override;


    template <typename T, typename... Args>
    inline void addStore(Args &&...args) {
      stores.push_back(std::make_shared<T>(std::forward<Args>(args)...));
    }

    inline void addEmbeddedSource() { addStore<ren::EmbeddedAssetSource>(); }

    inline void addFilesystem(const std::string_view &path) {
      addStore<ren::FilesystemAssetSource>(path);
    }

   private:
    std::vector<std::shared_ptr<AssetSource>> stores;
  };

}  // namespace ren