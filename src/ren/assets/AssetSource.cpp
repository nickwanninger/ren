#include <ren/assets/AssetSource.h>
#include <miniz/miniz.h>
#include <fstream>
#include <chrono>

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
  }



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



  bool FilesystemAssetSource::load(const std::string_view& name, std::vector<u8>& out) {
    std::filesystem::path assetPath;
    if (!resolve(name, assetPath)) {
      return false;
    }

    if (!std::filesystem::exists(assetPath) || !std::filesystem::is_regular_file(assetPath)) {
      return false;  // file does not exist or is not a regular file
    }

    // Open the file in binary mode.
    std::ifstream file(assetPath, std::ios::binary);
    if (!file) {
      return false;
    }

    // Determine file size
    file.seekg(0, std::ios::end);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (size < 0) {
      return false;  // some error occurred
    }



    // Read the whole file into `out`
    out.resize(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char*>(out.data()), size)) {
      out.clear();  // reading failed – return an empty vector
      return false;
    }

    return true;
  }


  bool FilesystemAssetSource::store(const std::string_view& name, ren::ByteSpan data) {
    std::filesystem::path assetPath = name;
    if (!resolve(name, assetPath)) {
      return false;
    }

    // Open the file in binary mode.
    std::ofstream file(assetPath, std::ios::binary);
    if (!file) {
      return false;
    }

    if (!file.write(reinterpret_cast<const char*>(data.data), data.size)) {
      return false;
    }

    // Close explicitly so that a failed flush is reported.
    file.close();
    if (!file) {
      return false;
    }

    return true;
  }



  bool FilesystemAssetSource::stat(const std::string_view& name, AssetStat& out) {
    std::filesystem::path path;
    if (!resolve(name, path)) {
      return false;
    }

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


  bool MultiAssetSource::store(const std::string_view& name, ren::ByteSpan data) {
    ren::println("Store {} to {}", data.size, name);
    for (auto& store : stores) {
      if (store->store(name, data)) {
        return true;
      }
    }
    return false;
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



  // Helper function for decompressing gzip data in EmbeddedAssetSource::load
  static int decompress_gzip(const unsigned char* compressed_data, size_t compressed_size, unsigned char* dest, size_t dest_size) {
    // Skip gzip header (10 bytes minimum, but variable with optional fields)
    // Simple case: skip 10 bytes, ignore 8-byte footer at end
    const unsigned char* deflate_data = compressed_data + 10;
    size_t deflate_size = compressed_size - 10 - 8;

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
    for (const auto& res : getEmbeddedResources()) {
      if (res.name == name) {
        // Decompress the data using miniz

        auto decompressed_size = res.uncompressed_size;
        out.resize(decompressed_size);

        // Decompress into the buffer
        int status = decompress_gzip(res.data, res.size, out.data(), out.size());
        if (status != MZ_OK) {
          ren::errln("[Asset Source] Failed to decompress embedded resource '{}', status={}", name, status);
          out.clear();
          return false;
        }
        return true;
      }
    }
    return false;
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
