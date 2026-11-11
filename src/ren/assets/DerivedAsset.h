#pragma once

#include <ren/assets/Asset.h>
#include <ren/assets/AssetSource.h>

namespace ren {

  // Parse an asset ID string. Throws if the text is not an ID or is the null ID.
  AssetID parseAssetID(const json &value);



  //=========================
  // Derived file layout
  //=========================

  // Bundles and records with a different version are rejected on read.
  inline constexpr int DERIVED_FORMAT_VERSION = 1;
  inline constexpr int IMPORT_RECORD_VERSION = 1;

  // Generated files are in this folder at the source root. Imports skip it.
  inline constexpr std::string_view DERIVED_ROOT = ".derived";

  // True if `path` is in the derived folder. `path` must be normalized.
  bool isDerivedPath(const std::filesystem::path &path);

  // The record path of an import, relative to the source root.
  std::string importRecordPath(AssetID sourceAsset);

  // The bundle path of a derived asset, relative to the derived folder. Records store this form.
  std::string derivedBundlePath(AssetID id);

  // Convert a path relative to the derived folder into a path relative to the source root.
  std::string derivedPath(std::string_view relative);



  //=========================
  // Bundles and records
  //=========================

  // Read and validate one bundle without the source file or a GPU. Throws on invalid data,
  // or if no asset class is registered for the bundle type.
  ref<Asset> readDerivedAsset(AssetSource &source, const std::string_view &name);

  // Write one self-contained bundle. Throws if validation or the write fails.
  void writeDerivedAsset(AssetSource &source, const std::string_view &name, const Asset &asset);

  // Read a published import record and validate its version, IDs, and resource paths.
  json readImportRecord(AssetSource &source, AssetID sourceAsset);

  // Read every output and verify its identity and ownership against the import record.
  // This uses only the record and bundles, without the source file or a GPU.
  std::vector<ref<Asset>> readImportedAssets(AssetSource &source, const json &record);

}  // namespace ren
