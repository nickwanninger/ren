#pragma once

#include <ren/assets/Asset.h>
#include <ren/assets/AssetSource.h>
#include <ren/assets/DerivedAsset.h>
#include <algorithm>

namespace ren::import_detail {

  // Normalize a source-relative name without filesystem access. Reject paths outside the source.
  inline std::filesystem::path sourcePath(const std::filesystem::path &path) {
    std::string name = path.generic_string();
    std::replace(name.begin(), name.end(), '\\', '/');
    auto normalized = std::filesystem::path(name).lexically_normal();
    if (normalized.empty() || normalized == "." || normalized.is_absolute() || normalized.has_root_name() || *normalized.begin() == ".." ||
        name.find(':') != std::string::npos || name.find('\0') != std::string::npos) {
      throw std::runtime_error("Invalid asset source path: " + name);
    }
    return normalized;
  }


  // Like sourcePath(), but returns None for a path outside the source.
  inline Option<std::filesystem::path> trySourcePath(const std::filesystem::path &path) {
    try {
      return sourcePath(path);
    } catch (const std::exception &) {
      return None;
    }
  }


  // Read all bytes through the source. Throws if the read fails.
  inline std::vector<u8> readBytes(AssetSource &source, const std::filesystem::path &path) {
    auto name = sourcePath(path).generic_string();
    std::vector<u8> bytes;
    if (!source.load(name, bytes)) {
      throw std::runtime_error("Cannot read import input: " + name);
    }
    return bytes;
  }


  // Hash an input through the source without a filesystem path.
  inline std::string fileHash(AssetSource &source, const std::filesystem::path &path) {
    auto name = sourcePath(path).generic_string();
    SHA256 hash;
    if (!source.updateSHA256(name, hash)) {
      throw std::runtime_error("Cannot hash import input: " + name);
    }
    return hash.getHash();
  }


  // Write JSON to a temporary file, then rename it to `name`, so that readers never see a partial file.
  // The parent directory must exist. Throws an error that names `what` if a step fails.
  inline void storeJSONAtomic(AssetSource &source, const std::string &name, const json &value, const std::string &what) {
    auto temporary = name + "." + AssetID::generate().to_string() + ".tmp";
    if (!source.storeJSON(temporary, value) || !source.rename(temporary, name)) {
      throw std::runtime_error("Cannot write " + what + ": " + name);
    }
  }


  // Publish only after all bundles succeed. The source supplies rename semantics.
  inline void publishRecord(AssetSource &source, AssetID id, const json &record) {
    auto name = importRecordPath(id);
    if (!source.mkdir(std::filesystem::path(name).parent_path().generic_string())) {
      throw std::runtime_error("Cannot create import record directory");
    }
    storeJSONAtomic(source, name, record, "import record");
  }

}  // namespace ren::import_detail
