#pragma once

#include <ren/types.h>
#include <vector>
#include <string_view>
#include <functional>
#include <filesystem>
#include <hash-library/sha256.h>

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
   * @brief How AssetSource::open opens a file.
   *
   * - `Read`: the file must exist.
   * - `Write`: the file must exist, unless `Create` is also set. The contents
   *   are kept, unless `Truncate` is also set.
   * - `Create` and `Truncate` require `Write`.
   *
   * `Read | Write` opens one file for both. The usual flags to replace a file
   * are `Write | Create | Truncate`.
   */
  enum class AssetOpenFlags : u8 {
    None = 0,
    Read = 1 << 0,
    Write = 1 << 1,
    Create = 1 << 2,    ///< Make the file if it does not exist.
    Truncate = 1 << 3,  ///< Set the length of the file to 0 when it opens.
  };
  REN_FLAG_ENUM(AssetOpenFlags, u8)

  /// @brief `true` if all bits of `bits` are set in `flags`.
  inline bool hasFlags(AssetOpenFlags flags, AssetOpenFlags bits) { return (flags & bits) == bits; }


  /**
   * @class AssetFile
   * @brief An open file from an AssetSource, for streaming access.
   *
   * The file has one cursor. `read` and `write` start at the cursor and move
   * it. `seek` moves it to an absolute offset. A handle is not thread safe;
   * each thread opens its own handle.
   *
   * The destructor closes the file. A caller that must know that its writes
   * reached the source calls `flush` and examines the result.
   */
  class AssetFile {
   public:
    virtual ~AssetFile() = default;

    /**
     * @brief Reads up to `dst.size` bytes at the cursor.
     * @return The number of bytes read. A short count means end of file or
     *         an error. Use `error` to tell the two apart.
     */
    virtual u64 read(ByteSpan dst) = 0;

    /**
     * @brief Writes `src` at the cursor.
     * @return The number of bytes written. A short count means an error. The
     *         default returns 0, for a file that is not open for `Write`.
     */
    virtual u64 write(ByteSpan src) { return 0; }

    /**
     * @brief Moves the cursor to `offset` bytes from the start of the file.
     *
     * A file that is not open for `Write` cannot seek past the end.
     */
    virtual bool seek(u64 offset) = 0;

    /// @brief The cursor, in bytes from the start of the file.
    virtual u64 tell() const = 0;

    /// @brief Gets the metadata of the open file.
    virtual bool stat(AssetStat &out) = 0;

    /// @brief Sends buffered writes to the source. The default returns `true`.
    virtual bool flush() { return true; }

    /// @brief `true` after a read or a write failed for a reason other than end of file.
    virtual bool error() const = 0;


    Option<std::string> readAllText();
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
   * Only `open` is required. `load` and `store` have default
   * implementations that use `open`. A source overrides them only when it
   * has a faster path. All other operations are optional, and the default
   * implementation of each returns `false` ("not supported").
   * A read-only source (embedded data, asset packs) implements `open` for
   * `Read` only, `stat`, `list` and `enumerate`. A writable source also
   * implements `open` for `Write`, `mkdir`, `remove` and `rename`.
   *
   */
  class AssetSource {
   public:
    virtual ~AssetSource() = default;

    /**
     * @brief Opens the file `name` for streaming access.
     *
     * The parent directory must exist for `Create`. Use `mkdir` to make it
     * first. A read-only source returns `nullptr` for all flags except `Read`.
     *
     * @return The open file, or `nullptr` if the file cannot be opened with `flags`.
     */
    virtual ref<AssetFile> open(const std::string_view &name, AssetOpenFlags flags) = 0;

    /**
     * @brief Loads the raw byte contents of an asset file identified by `name`
     * @param name  The logical name (path) of the asset to load.
     * @param out   The destination for the loaded data.
     * @return     `true` if the asset was loaded, `false` otherwise.
     */
    virtual bool load(const std::string_view &name, std::vector<u8> &out);

    /**
     * @brief Writes `data` to the file `name`, and replaces the file if it exists.
     *
     * The parent directory must exist. Use `mkdir` to make it first.
     *
     * @param name  The logical name (path) of the file to write.
     * @param data  The bytes to write.
     * @return     `true` if all of the data was written and flushed, `false` otherwise.
     */
    virtual bool store(const std::string_view &name, ren::ByteSpan data);


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


    /**
     * @brief Updates a SHA256 hasher with the contents of the file `name`.
     *
     * @param name   The name of the file to hash.
     * @param hasher The SHA256 hasher to update.
     * @param buffer A buffer to use for reading the file in chunks. The size of
     *               the buffer determines the chunk size. It is recommended to
     *               use a buffer of at least 64 KiB for performance.
     * @returns `true` if the file was read and the hasher updated, `false` otherwise.
     */
    bool updateSHA256(const std::string_view &name, SHA256 &hasher, std::vector<u8> &buffer);
    bool updateSHA256(const std::string_view &name, SHA256 &hasher) {
      std::vector<u8> buffer(64 * 1024);
      return updateSHA256(name, hasher, buffer);
    }


    /**
     * @brief Reads a file for its json value.
     * 
     * This exists mostly for convenience and avoids having to write the same
     * boilerplate code over and over again.
     *
     * @param name The name of the file to read.
     * @return The json value of the file, or an empty json object if the file is not found or is not valid json.
     */
    Option<json> loadJSON(const std::string_view &name);
  };




  // ------ //
  class FilesystemAssetSource final : public AssetSource {
   public:
    FilesystemAssetSource(const std::filesystem::path &root);

    ~FilesystemAssetSource() override;
    ref<AssetFile> open(const std::string_view &name, AssetOpenFlags flags) override;
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
    ref<AssetFile> open(const std::string_view &name, AssetOpenFlags flags) override;
    // One inflate call into a buffer of known size is faster than the
    // streaming path that the default `load` uses.
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
    ref<AssetFile> open(const std::string_view &name, AssetOpenFlags flags) override;
    // Forwards to the `load` of each child, so that a child keeps its fast path.
    bool load(const std::string_view &name, std::vector<u8> &out) override;
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

    inline void addFilesystem(const std::string_view &path) { addStore<ren::FilesystemAssetSource>(path); }

   private:
    std::vector<std::shared_ptr<AssetSource>> stores;
  };

}  // namespace ren
