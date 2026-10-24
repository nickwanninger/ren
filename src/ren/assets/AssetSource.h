#pragma once

#include <ren/types.h>
#include <vector>
#include <string>

#include <functional>
#include <filesystem>

namespace ren {

  enum class AssetEntryType : u8 {
    File,
    Directory,
  };


  /**
   * @brief Metadata for one entry in an AssetSource.
   *
   * `mtime` is in nanoseconds since the Unix epoch. A source that does not
   * track modification time sets it to 0.
   */
  struct AssetStat {
    AssetEntryType type = AssetEntryType::File;
    u64 size = 0;  ///< Size in bytes. Always 0 for directories.
    i64 mtime = 0;
  };


  /**
   * @brief One entry of a directory listing, as given by AssetSource::list.
   *
   * `name` is the leaf name of the entry (no parent path). It is only valid
   * for the duration of the callback.
   */
  struct AssetDirEntry {
    std::string_view name;
    AssetEntryType type;
  };


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
   * Names are relative paths that use '/' as the separator. The empty
   * name "" refers to the root of the source. A name must not be absolute
   * and must not go above the root with "..".
   *
   * Only `load` is required. All other operations are optional, and the
   * default implementation of each returns `false` ("not supported").
   * A read-only source (embedded data, asset packs) implements `load`,
   * `stat`, `list` and `enumerate`. A writable source also implements
   * `store`, `mkdir`, `remove` and `rename`.
   *
   */
  class AssetSource {
   public:
    virtual ~AssetSource() = default;

    /**
     * @brief Loads the raw byte contents of an asset file identified by `name`
     * @param name  The logical name (path) of the asset to load.
     * @param out   The destination for the loaded data.
     * @return     `true` if the asset was loaded, `false` otherwise.
     */
    virtual bool load(const std::string_view &name, std::vector<u8> &out) = 0;

    /**
     * @brief Writes `data` to the file `name`, and replaces the file if it exists.
     *
     * The parent directory must exist. Use `mkdir` to make it first.
     *
     * @param name  The logical name (path) of the file to write.
     * @param data  The bytes to write.
     * @return     `true` if all of the data was written, `false` otherwise.
     */
    virtual bool store(const std::string_view &name, ren::ByteSpan data) { return false; }


    /**
     * @brief Gets the metadata of the file or directory `name`.
     * @return `true` if the entry exists and `out` was filled, `false` otherwise.
     */
    virtual bool stat(const std::string_view &name, AssetStat &out) { return false; }


    /**
     * @brief Calls `callback` one time for each direct child of the directory `name`.
     *
     * This is not recursive, and the order of the entries is not defined.
     * The entries "." and ".." are not given.
     *
     * @return `true` if `name` is a directory and the listing completed,
     *         `false` otherwise.
     */
    virtual bool list(const std::string_view &name, std::function<void(const AssetDirEntry &)> callback) { return false; }


    /**
     * @brief Makes the directory `name`, and all of its missing parents (like `mkdir -p`).
     * @return `true` if the directory exists when the call returns, `false` otherwise.
     */
    virtual bool mkdir(const std::string_view &name) { return false; }


    /**
     * @brief Removes the file or the empty directory `name`.
     *
     * This is not recursive. A directory that is not empty is not removed.
     * Use `removeAll` to remove a directory and its contents.
     *
     * @return `true` if the entry was removed, `false` otherwise (this
     *         includes when the entry does not exist).
     */
    virtual bool remove(const std::string_view &name) { return false; }


    /**
     * @brief Moves the file or directory `from` to `to`, and replaces `to` if it is a file.
     *
     * Where the source can, this is atomic. Thus, a caller can write to a
     * temporary name and then rename it into position, so that a reader
     * never sees a partial file.
     *
     * @return `true` if the entry was moved, `false` otherwise.
     */
    virtual bool rename(const std::string_view &from, const std::string_view &to) { return false; }


    /// @brief `true` if the file or directory `name` exists.
    bool exists(const std::string_view &name);

    /// @brief `true` if `name` exists and is a file.
    bool isFile(const std::string_view &name);

    /// @brief `true` if `name` exists and is a directory.
    bool isDirectory(const std::string_view &name);

    /**
     * @brief Removes `name` and, if it is a directory, all of its contents (like `rm -r`).
     *
     * This uses only `stat`, `list` and `remove`, thus it works on all
     * sources that implement those operations.
     *
     * @return `true` if `name` does not exist when the call returns, `false` otherwise.
     */
    bool removeAll(const std::string_view &name);


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
    bool store(const std::string_view &name, ren::ByteSpan data) override;
    bool stat(const std::string_view &name, AssetStat &out) override;
    bool list(const std::string_view &name, std::function<void(const AssetDirEntry &)> callback) override;
    bool mkdir(const std::string_view &name) override;
    bool remove(const std::string_view &name) override;
    bool rename(const std::string_view &from, const std::string_view &to) override;
    bool enumerate(std::function<void(const std::string_view &)> callback) override;



   private:
    // Converts `name` to a path under m_root. Returns false if `name` is
    // absolute or goes above the root.
    bool resolve(const std::string_view &name, std::filesystem::path &out) const;

    std::filesystem::path m_root;  ///< Directory that serves as the root.
  };


  // EmbeddedAssetSource - for assets compiled into the binary
  class EmbeddedAssetSource final : public AssetSource {
   public:
    EmbeddedAssetSource() = default;
    ~EmbeddedAssetSource() override = default;
    bool load(const std::string_view &name, std::vector<u8> &out) override;
    bool stat(const std::string_view &name, AssetStat &out) override;
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
   *     caller can filter duplicates if desired. `list` does the same.
   *   * All other operations go to the first child store that succeeds.
   *
   * @see AssetSource
   * @see emplace
   */
  class MultiAssetSource : public AssetSource {
   public:
    ~MultiAssetSource() override = default;
    bool load(const std::string_view &name, std::vector<u8> &out) override;
    bool store(const std::string_view &name, ren::ByteSpan data) override;
    bool stat(const std::string_view &name, AssetStat &out) override;
    bool list(const std::string_view &name, std::function<void(const AssetDirEntry &)> callback) override;
    bool mkdir(const std::string_view &name) override;
    bool remove(const std::string_view &name) override;
    bool rename(const std::string_view &from, const std::string_view &to) override;
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
