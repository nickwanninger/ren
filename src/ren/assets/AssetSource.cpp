#include <ren/assets/AssetSource.h>

namespace ren {


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

    fmt::println("Created Filesystem Asset Manager with root '{}'", m_root.c_str());
  }



  bool FilesystemAssetSource::load(const std::string_view& name, std::vector<u8>& out) {
    const std::filesystem::path assetPath = m_root / std::filesystem::path(name);


    if (!std::filesystem::exists(assetPath) || !std::filesystem::is_regular_file(assetPath)) {
      return false;  // file does not exist or is not a regular file
    }

    // Open the file in binary mode.
    std::ifstream file(assetPath, std::ios::binary);
    if (!file) return false;

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


  bool FilesystemAssetSource::enumerate(std::function<void(const std::string_view&)> callback) {
    // Quick sanity check – does the root exist and is it a directory?
    std::error_code ec;
    if (!std::filesystem::exists(m_root, ec) || !std::filesystem::is_directory(m_root, ec)) {
      return false;  // nothing to enumerate
    }

    // Recursive iterator – skip unreadable directories instead of throwing
    try {
      for (const auto& entry : std::filesystem::recursive_directory_iterator(
               m_root, std::filesystem::directory_options::skip_permission_denied)) {
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
      if (store->load(name, out)) { return true; }
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




}  // namespace ren