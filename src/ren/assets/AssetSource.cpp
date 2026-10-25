#include <ren/assets/AssetSource.h>
#include <miniz/miniz.h>
#include <fstream>
#include <algorithm>
#include <limits>
#include <chrono>
#include <mutex>

namespace ren {


  bool AssetSource::exists(const std::string_view& name) {
    AssetStat st;
    return stat(name, st);
  }


  bool AssetSource::isFile(const std::string_view& name) {
    AssetStat st;
    return stat(name, st) && st.type == AssetEntryType::File;
  }


  bool AssetSource::isDirectory(const std::string_view& name) {
    AssetStat st;
    return stat(name, st) && st.type == AssetEntryType::Directory;
  }


  bool AssetSource::removeAll(const std::string_view& name) {
    AssetStat st;
    if (!stat(name, st)) {
      return true;  // nothing to remove
    }

    if (st.type == AssetEntryType::Directory) {
      // Collect the children first, so that we do not remove entries
      // while the source iterates over the directory.
      std::vector<std::string> children;
      bool listed = list(name, [&](const AssetDirEntry& entry) {
        if (name.empty()) {
          children.emplace_back(entry.name);
        } else {
          children.push_back(fmt::format("{}/{}", name, entry.name));
        }
      });
      if (!listed) {
        return false;
      }

      for (const auto& child : children) {
        if (!removeAll(child)) {
          return false;
        }
      }
    }

    return remove(name);
  }


  bool AssetSource::load(const std::string_view& name, std::vector<u8>& out) {
    auto file = open(name, AssetOpenFlags::Read);
    if (!file) {
      return false;
    }

    AssetStat st;
    if (!file->stat(st)) {
      return false;
    }

    out.resize(st.size);
    if (file->read(ByteSpan(out.data(), out.size())) != st.size) {
      out.clear();
      return false;
    }
    return true;
  }


  bool AssetSource::store(const std::string_view& name, ren::ByteSpan data) {
    auto file = open(name, AssetOpenFlags::Write | AssetOpenFlags::Create | AssetOpenFlags::Truncate);
    if (!file) {
      return false;
    }

    if (file->write(data) != data.size) {
      return false;
    }
    return file->flush();
  }



  bool AssetSource::updateSHA256(const std::string_view& name, SHA256& hasher, std::vector<u8>& chunk) {
    auto file = open(name, AssetOpenFlags::Read);
    if (!file) {
      return false;
    }
    while (u64 count = file->read(ByteSpan(chunk))) {
      hasher.add(chunk.data(), count);
    }
    return !file->error();
  }




  FilesystemAssetSource::FilesystemAssetSource(const std::filesystem::path& root)
      : m_root(root) {
    // Normalize the root (resolve symlinks, etc.) without
    // throwing.  Any errors are ignored – the caller will see
    // a `false` result if a file cannot be found.
    std::error_code ec;
    m_root = std::filesystem::canonical(m_root, ec);
    if (ec) {
      // If canonicalization fails (e.g. the path does not yet
      // exist) fall back to the original value.
      m_root = root;
    }

    if (!std::filesystem::is_directory(m_root, ec)) {
      return;
    }
  }


  FilesystemAssetSource::~FilesystemAssetSource() = default;

  bool FilesystemAssetSource::resolve(const std::string_view& name, std::filesystem::path& out) const {
    std::filesystem::path rel = std::filesystem::path(name).lexically_normal();
    if (rel.has_root_path()) {
      return false;  // absolute names are not permitted
    }
    if (!rel.empty() && *rel.begin() == "..") {
      return false;  // the name goes above the root
    }
    if (rel == ".") {
      rel.clear();
    }

    out = rel.empty() ? m_root : m_root / rel;
    return true;
  }



  // Fills `out` for the file or directory at `path`. Special files (sockets,
  // devices, ...) are not entries of a source.
  static bool statPath(const std::filesystem::path& path, AssetStat& out) {
    std::error_code ec;
    auto status = std::filesystem::status(path, ec);
    if (ec) {
      return false;
    }

    if (std::filesystem::is_directory(status)) {
      out.type = AssetEntryType::Directory;
      out.size = 0;
    } else if (std::filesystem::is_regular_file(status)) {
      out.type = AssetEntryType::File;
      out.size = std::filesystem::file_size(path, ec);
      if (ec) {
        return false;
      }
    } else {
      return false;  // does not exist, or is a special file (socket, device, ...)
    }

    auto writeTime = std::filesystem::last_write_time(path, ec);
    if (ec) {
      return false;
    }
    auto sysTime = std::chrono::file_clock::to_sys(writeTime);
    out.mtime = std::chrono::duration_cast<std::chrono::nanoseconds>(sysTime.time_since_epoch()).count();

    return true;
  }



  // An open file of a FilesystemAssetSource.
  //
  // A std::filebuf has one position for reads and writes, and the C stream
  // rules require a seek between a read and a write. Thus, the file keeps its
  // own cursor and seeks the stream when the operation changes.
  class FilesystemAssetFile final : public AssetFile {
   public:
    FilesystemAssetFile(std::filesystem::path path, std::fstream&& stream, AssetOpenFlags flags)
        : m_path(std::move(path))
        , m_stream(std::move(stream))
        , m_flags(flags) {}


    u64 read(ByteSpan dst) override {
      if (!hasFlags(m_flags, AssetOpenFlags::Read) || dst.size == 0) {
        return 0;
      }
      if (m_lastOp != Op::Read) {
        m_stream.seekg(static_cast<std::streamoff>(m_cursor));
        m_lastOp = Op::Read;
      }

      m_stream.read(reinterpret_cast<char*>(dst.data), static_cast<std::streamsize>(dst.size));
      u64 count = static_cast<u64>(m_stream.gcount());
      m_cursor += count;

      // A short read at end of file sets failbit and eofbit. That is not an
      // error, but the stream state must be cleared for the next operation.
      if (m_stream.bad() || (m_stream.fail() && !m_stream.eof())) {
        m_error = true;
      }
      m_stream.clear();
      return count;
    }


    u64 write(ByteSpan src) override {
      if (!hasFlags(m_flags, AssetOpenFlags::Write) || src.size == 0) {
        return 0;
      }
      if (m_lastOp != Op::Write) {
        m_stream.seekp(static_cast<std::streamoff>(m_cursor));
        m_lastOp = Op::Write;
      }

      // An ostream does not tell how many bytes of a failed write went out.
      m_stream.write(reinterpret_cast<const char*>(src.data), static_cast<std::streamsize>(src.size));
      if (!m_stream) {
        m_error = true;
        m_stream.clear();
        return 0;
      }
      m_cursor += src.size;
      return src.size;
    }


    bool seek(u64 offset) override {
      if (!hasFlags(m_flags, AssetOpenFlags::Write)) {
        AssetStat st;
        if (!stat(st) || offset > st.size) {
          return false;
        }
      }

      // The next read or write seeks the stream.
      m_cursor = offset;
      m_lastOp = Op::None;
      return true;
    }


    u64 tell() const override { return m_cursor; }


    bool stat(AssetStat& out) override {
      // Buffered writes are not in the size that the file system reports.
      if (hasFlags(m_flags, AssetOpenFlags::Write) && !flush()) {
        return false;
      }
      return statPath(m_path, out);
    }


    bool flush() override {
      if (!hasFlags(m_flags, AssetOpenFlags::Write)) {
        return true;
      }
      m_stream.flush();
      if (!m_stream) {
        m_error = true;
        m_stream.clear();
        return false;
      }
      return true;
    }


    bool error() const override { return m_error; }


   private:
    enum class Op : u8 { None, Read, Write };

    std::filesystem::path m_path;
    std::fstream m_stream;
    AssetOpenFlags m_flags;
    u64 m_cursor = 0;
    Op m_lastOp = Op::None;  ///< None forces a seek before the next read or write.
    bool m_error = false;
  };



  ref<AssetFile> FilesystemAssetSource::open(const std::string_view& name, AssetOpenFlags flags) {
    bool read = hasFlags(flags, AssetOpenFlags::Read);
    bool write = hasFlags(flags, AssetOpenFlags::Write);
    bool create = hasFlags(flags, AssetOpenFlags::Create);
    bool truncate = hasFlags(flags, AssetOpenFlags::Truncate);
    if ((!read && !write) || ((create || truncate) && !write)) {
      return nullptr;
    }

    std::filesystem::path path;
    if (!resolve(name, path)) {
      return nullptr;
    }

    std::error_code ec;
    bool exists = std::filesystem::exists(path, ec);
    if (exists && !std::filesystem::is_regular_file(path, ec)) {
      return nullptr;  // a directory or a special file
    }
    if (!exists && !create) {
      return nullptr;
    }

    // An fstream open mode cannot express "create, but do not truncate":
    // `in | out` requires the file to exist, and `out` alone truncates. Thus,
    // make an empty file first, and then open it with `in | out`. Another
    // process can make the file between these two steps. That is acceptable
    // for an asset directory.
    std::ios::openmode mode = std::ios::binary;
    if (truncate) {
      mode |= std::ios::out | std::ios::trunc;
      if (read) {
        mode |= std::ios::in;
      }
    } else if (write) {
      if (!exists && !std::ofstream(path, std::ios::binary)) {
        return nullptr;
      }
      mode |= std::ios::in | std::ios::out;
    } else {
      mode |= std::ios::in;
    }

    std::fstream stream(path, mode);
    if (!stream) {
      return nullptr;
    }
    return make<FilesystemAssetFile>(std::move(path), std::move(stream), flags);
  }



  bool FilesystemAssetSource::stat(const std::string_view& name, AssetStat& out) {
    std::filesystem::path path;
    if (!resolve(name, path)) {
      return false;
    }
    return statPath(path, out);
  }


  bool FilesystemAssetSource::list(const std::string_view& name, std::function<void(const AssetDirEntry&)> callback) {
    std::filesystem::path path;
    if (!resolve(name, path)) {
      return false;
    }

    std::error_code ec;
    std::filesystem::directory_iterator it(path, std::filesystem::directory_options::skip_permission_denied, ec);
    if (ec) {
      return false;  // does not exist, or is not a directory
    }

    for (; it != std::filesystem::directory_iterator(); it.increment(ec)) {
      if (ec) {
        return false;
      }

      AssetEntryType type;
      if (it->is_directory(ec)) {
        type = AssetEntryType::Directory;
      } else if (it->is_regular_file(ec)) {
        type = AssetEntryType::File;
      } else {
        continue;  // special file, or an error reading the entry
      }

      std::string leaf = it->path().filename().string();
      callback(AssetDirEntry{leaf, type});
    }

    return !ec;
  }


  bool FilesystemAssetSource::mkdir(const std::string_view& name) {
    std::filesystem::path path;
    if (!resolve(name, path)) {
      return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    // create_directories gives an error if the path exists as a file, but
    // not if it exists as a directory. Thus, test the result directly.
    return std::filesystem::is_directory(path, ec);
  }


  bool FilesystemAssetSource::remove(const std::string_view& name) {
    std::filesystem::path path;
    if (!resolve(name, path)) {
      return false;
    }
    if (path == m_root) {
      return false;  // never remove the root
    }

    // std::filesystem::remove does not remove a directory that is not empty.
    std::error_code ec;
    return std::filesystem::remove(path, ec) && !ec;
  }


  bool FilesystemAssetSource::rename(const std::string_view& from, const std::string_view& to) {
    std::filesystem::path fromPath, toPath;
    if (!resolve(from, fromPath) || !resolve(to, toPath)) {
      return false;
    }
    if (fromPath == m_root || toPath == m_root) {
      return false;
    }

    std::error_code ec;
    std::filesystem::rename(fromPath, toPath, ec);
    return !ec;
  }



  bool FilesystemAssetSource::enumerate(std::function<void(const std::string_view&)> callback) {
    // Quick sanity check – does the root exist and is it a directory?
    std::error_code ec;
    if (!std::filesystem::exists(m_root, ec) || !std::filesystem::is_directory(m_root, ec)) {
      return false;  // nothing to enumerate
    }

    // Recursive iterator – skip unreadable directories instead of throwing
    try {
      for (const auto& entry : std::filesystem::recursive_directory_iterator(m_root, std::filesystem::directory_options::skip_permission_denied)) {
        if (!entry.is_regular_file(ec) || ec) {
          continue;  // not a regular file (or an error reading the entry)
        }

        // Compute the key relative to the root; keep it in a portable form
        std::string rel = entry.path().lexically_relative(m_root).string();

        // Pass a string_view that outlives the callback invocation
        callback(std::string_view{rel});
      }
    } catch (const std::filesystem::filesystem_error&) {
      // An unexpected I/O error – report failure to the caller
      return false;
    }

    return true;
  }




  bool MultiAssetSource::load(const std::string_view& name, std::vector<u8>& out) {
    for (auto& store : stores) {
      if (store->load(name, out)) {
        return true;
      }
    }
    return false;
  }


  ref<AssetFile> MultiAssetSource::open(const std::string_view& name, AssetOpenFlags flags) {
    // A read-only child refuses `Write`, so a write goes to the first
    // writable child that accepts it.
    for (auto& store : stores) {
      if (auto file = store->open(name, flags)) {
        return file;
      }
    }
    return nullptr;
  }


  bool MultiAssetSource::stat(const std::string_view& name, AssetStat& out) {
    for (auto& store : stores) {
      if (store->stat(name, out)) {
        return true;
      }
    }
    return false;
  }


  bool MultiAssetSource::list(const std::string_view& name, std::function<void(const AssetDirEntry&)> callback) {
    bool listed = false;
    for (auto& store : stores) {
      listed |= store->list(name, callback);
    }
    return listed;
  }


  bool MultiAssetSource::mkdir(const std::string_view& name) {
    for (auto& store : stores) {
      if (store->mkdir(name)) {
        return true;
      }
    }
    return false;
  }


  bool MultiAssetSource::remove(const std::string_view& name) {
    for (auto& store : stores) {
      if (store->remove(name)) {
        return true;
      }
    }
    return false;
  }


  bool MultiAssetSource::rename(const std::string_view& from, const std::string_view& to) {
    for (auto& store : stores) {
      if (store->rename(from, to)) {
        return true;
      }
    }
    return false;
  }


  bool MultiAssetSource::enumerate(std::function<void(const std::string_view&)> callback) {
    bool enumerated = false;
    for (auto& store : stores) {
      enumerated |= store->enumerate(callback);
    }
    return enumerated;
  }


  // ------------------------ //

  // embedded asset resources are

  struct EmbeddedResource {
    std::string name;
    const u8* data;
    unsigned long size;
    unsigned long uncompressed_size;
  };


  static std::vector<EmbeddedResource>& getEmbeddedResources() {
    static std::vector<EmbeddedResource> resources;
    return resources;
  }



  static const EmbeddedResource* findEmbeddedResource(const std::string_view& name) {
    for (const auto& res : getEmbeddedResources()) {
      if (res.name == name) {
        return &res;
      }
    }
    return nullptr;
  }



  // Finds the raw deflate data in a gzip member (RFC 1952). The header has
  // optional fields after its first 10 bytes, and the member ends with an
  // 8-byte footer (CRC-32 and size).
  static bool findDeflateData(const u8* data, size_t size, const u8*& outData, size_t& outSize) {
    constexpr u8 FHCRC = 1 << 1;
    constexpr u8 FEXTRA = 1 << 2;
    constexpr u8 FNAME = 1 << 3;
    constexpr u8 FCOMMENT = 1 << 4;
    constexpr size_t FOOTER_SIZE = 8;

    if (size < 10 + FOOTER_SIZE || data[0] != 0x1f || data[1] != 0x8b || data[2] != 8) {
      return false;  // not gzip, or not deflate
    }
    u8 flags = data[3];
    size_t pos = 10;
    size_t end = size - FOOTER_SIZE;

    if ((flags & FEXTRA) != 0) {
      if (pos + 2 > end) {
        return false;
      }
      pos += 2 + (size_t(data[pos]) | size_t(data[pos + 1]) << 8);
    }

    // FNAME and FCOMMENT are zero-terminated strings.
    for (u8 field : {FNAME, FCOMMENT}) {
      if ((flags & field) == 0) {
        continue;
      }
      while (pos < end && data[pos] != 0) {
        pos++;
      }
      pos++;
    }

    if ((flags & FHCRC) != 0) {
      pos += 2;
    }

    if (pos > end) {
      return false;
    }
    outData = data + pos;
    outSize = end - pos;
    return true;
  }



  // Helper function for decompressing gzip data in EmbeddedAssetSource::load
  static int decompress_gzip(const unsigned char* compressed_data, size_t compressed_size, unsigned char* dest, size_t dest_size) {
    const u8* deflate_data;
    size_t deflate_size;
    if (!findDeflateData(compressed_data, compressed_size, deflate_data, deflate_size)) {
      return MZ_DATA_ERROR;
    }

    mz_stream stream = {0};
    stream.next_in = deflate_data;
    stream.avail_in = deflate_size;
    stream.next_out = dest;
    stream.avail_out = dest_size;

    // -15 = raw deflate (no wrapper)
    int status = mz_inflateInit2(&stream, -15);
    if (status != MZ_OK) {
      return status;
    }

    status = mz_inflate(&stream, MZ_FINISH);
    mz_inflateEnd(&stream);

    if (status != MZ_STREAM_END) {
      return status;
    }

    return MZ_OK;
  }

  bool EmbeddedAssetSource::load(const std::string_view& name, std::vector<u8>& out) {
    const EmbeddedResource* res = findEmbeddedResource(name);
    if (res == nullptr) {
      return false;
    }

    out.resize(res->uncompressed_size);
    int status = decompress_gzip(res->data, res->size, out.data(), out.size());
    if (status != MZ_OK) {
      ren::errln("[Asset Source] Failed to decompress embedded resource '{}', status={}", name, status);
      out.clear();
      return false;
    }
    return true;
  }



  // An open file of an EmbeddedAssetSource. It inflates the gzip data on
  // demand as the cursor moves forward, thus a sequential read never holds
  // the full uncompressed file.
  //
  // Deflate data has no random access. A forward seek inflates the skipped
  // bytes and discards them. A backward seek starts again from the beginning.
  class EmbeddedAssetFile final : public AssetFile {
   public:
    explicit EmbeddedAssetFile(const EmbeddedResource& res)
        : m_res(res) {}

    ~EmbeddedAssetFile() override {
      if (m_started) {
        mz_inflateEnd(&m_stream);
      }
    }

    EmbeddedAssetFile(const EmbeddedAssetFile&) = delete;
    EmbeddedAssetFile& operator=(const EmbeddedAssetFile&) = delete;


    // Starts the inflate stream at offset 0. Returns false if the gzip data is not valid.
    bool restart() {
      const u8* deflateData;
      size_t deflateSize;
      if (!findDeflateData(m_res.data, m_res.size, deflateData, deflateSize)) {
        return false;
      }

      if (m_started) {
        mz_inflateEnd(&m_stream);
        m_started = false;
      }
      m_stream = {};
      // -15 = raw deflate (no wrapper)
      if (mz_inflateInit2(&m_stream, -15) != MZ_OK) {
        return false;
      }
      m_started = true;

      m_stream.next_in = deflateData;
      m_stream.avail_in = static_cast<unsigned int>(deflateSize);
      m_cursor = 0;
      m_finished = false;
      return true;
    }


    u64 read(ByteSpan dst) override {
      if (m_error) {
        return 0;
      }

      u64 total = 0;
      while (total < dst.size && !m_finished) {
        // avail_out is 32 bits, thus a large read takes more than one call.
        unsigned int chunk = static_cast<unsigned int>(std::min<u64>(dst.size - total, std::numeric_limits<unsigned int>::max()));
        m_stream.next_out = dst.data + total;
        m_stream.avail_out = chunk;

        int status = mz_inflate(&m_stream, MZ_SYNC_FLUSH);
        total += chunk - m_stream.avail_out;

        if (status == MZ_STREAM_END) {
          m_finished = true;
        } else if (status != MZ_OK) {
          // All input is available, thus MZ_BUF_ERROR also means the data is truncated.
          m_error = true;
          break;
        }
      }

      m_cursor += total;
      if (m_finished && m_cursor != m_res.uncompressed_size) {
        m_error = true;  // the data does not agree with the recorded size
      }
      return total;
    }


    bool seek(u64 offset) override {
      if (offset > m_res.uncompressed_size) {
        return false;
      }
      if (offset < m_cursor && !restart()) {
        m_error = true;
        return false;
      }

      u8 scratch[16 * 1024];
      while (m_cursor < offset) {
        u64 count = std::min<u64>(offset - m_cursor, sizeof(scratch));
        if (read(ByteSpan(scratch, count)) != count) {
          return false;
        }
      }
      return true;
    }


    u64 tell() const override { return m_cursor; }


    bool stat(AssetStat& out) override {
      out.type = AssetEntryType::File;
      out.size = m_res.uncompressed_size;
      out.mtime = 0;
      return true;
    }


    bool error() const override { return m_error; }


   private:
    // A copy, because the resource list can grow and move its elements.
    // The bytes it points to live for the life of the program.
    EmbeddedResource m_res;
    mz_stream m_stream = {};
    bool m_started = false;  ///< `true` while m_stream must be ended.
    bool m_finished = false;
    bool m_error = false;
    u64 m_cursor = 0;
  };



  ref<AssetFile> EmbeddedAssetSource::open(const std::string_view& name, AssetOpenFlags flags) {
    // Embedded data is part of the binary, thus it is read-only.
    if (flags != AssetOpenFlags::Read) {
      return nullptr;
    }

    const EmbeddedResource* res = findEmbeddedResource(name);
    if (res == nullptr) {
      return nullptr;
    }

    auto file = make<EmbeddedAssetFile>(*res);
    if (!file->restart()) {
      ren::errln("[Asset Source] Embedded resource '{}' is not valid gzip data", name);
      return nullptr;
    }
    return file;
  }


  bool EmbeddedAssetSource::stat(const std::string_view& name, AssetStat& out) {
    // Embedded resources are a flat list of names. A directory exists if
    // it is the root, or if a resource name starts with "<name>/".
    bool isDir = name.empty();
    for (const auto& res : getEmbeddedResources()) {
      if (res.name == name) {
        out.type = AssetEntryType::File;
        out.size = res.uncompressed_size;
        out.mtime = 0;
        return true;
      }
      if (!isDir && res.name.size() > name.size() && res.name.starts_with(name) && res.name[name.size()] == '/') {
        isDir = true;
      }
    }

    if (isDir) {
      out.type = AssetEntryType::Directory;
      out.size = 0;
      out.mtime = 0;
    }
    return isDir;
  }


  bool EmbeddedAssetSource::enumerate(std::function<void(const std::string_view&)> callback) {
    bool enumerated = false;
    for (const auto& res : getEmbeddedResources()) {
      callback(std::string_view{res.name});
      enumerated = true;
    }
    return enumerated;
  }




}  // namespace ren


extern "C" {
void __ren_register_embedded_resource(const char* name, const unsigned char* data, unsigned long size, unsigned long original_size) {
  ren::getEmbeddedResources().push_back({name, data, size, original_size});
}
}
