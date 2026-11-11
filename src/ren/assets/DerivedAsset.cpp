#include <ren/assets/DerivedAsset.h>
#include <ren/core/Bundle.h>

#include <limits>
#include <set>

namespace ren {

  AssetID parseAssetID(const json &value) {
    auto text = value.get<std::string>();
    auto id = AssetID::parse(text);
    if (!id || static_cast<u64>(id.unwrap()) == UUID::null) {
      throw std::runtime_error("Invalid asset ID: " + text);
    }
    return id.unwrap();
  }


  //=========================
  // Derived file layout
  //=========================

  bool isDerivedPath(const std::filesystem::path &path) { return !path.empty() && *path.begin() == DERIVED_ROOT; }


  std::string importRecordPath(AssetID sourceAsset) { return derivedPath("imports/" + sourceAsset.to_string() + ".json"); }


  std::string derivedBundlePath(AssetID id) { return "resources/" + id.to_string() + ".asset"; }


  std::string derivedPath(std::string_view relative) { return std::string(DERIVED_ROOT) + "/" + std::string(relative); }



  //=========================
  // Validation
  //=========================

  static void validateRecord(const json &record) {
    if (record.at("version") != IMPORT_RECORD_VERSION) {
      throw std::runtime_error("Unsupported import record version");
    }
    parseAssetID(record.at("sourceAsset"));
    if (!record.at("derivedAssets").is_array()) {
      throw std::runtime_error("Import outputs must be an array");
    }

    std::set<AssetID> ids;
    for (const auto &output : record.at("derivedAssets")) {
      auto id = parseAssetID(output.at("id"));
      if (!ids.insert(id).second || output.at("path") != derivedBundlePath(id)) {
        throw std::runtime_error("Invalid derived asset path or duplicate ID: " + id.to_string());
      }
      auto type = parseEnum<AssetType>(output.at("type"), "import output type");
      if (!findAssetType(type)) {
        throw std::runtime_error("Unsupported import output type");
      }
    }
  }



  //=========================
  // Bundles
  //=========================

  ref<Asset> readDerivedAsset(AssetSource &source, const std::string_view &name) {
    try {
      std::vector<u8> bytes;
      if (!source.load(name, bytes)) {
        throw std::runtime_error("Cannot read bundle");
      }

      Bundle bundle(bytes);
      const auto &meta = bundle.getMetadata();
      if (meta.at("formatVersion") != DERIVED_FORMAT_VERSION) {
        throw std::runtime_error("Unsupported asset format version");
      }

      const auto *type = findAssetType(parseEnum<AssetType>(meta.at("type"), "asset type"));
      if (!type) {
        throw std::runtime_error("Unsupported asset type: " + meta.at("type").get<std::string>());
      }

      auto asset = type->read(bundle);
      asset->id = parseAssetID(meta.at("id"));
      asset->sourceAsset = parseAssetID(meta.at("sourceAsset"));
      asset->name = bundle.getName();
      asset->validate();
      return asset;
    } catch (const std::exception &error) {
      throw std::runtime_error(std::string(name) + ": " + error.what());
    }
  }


  void writeDerivedAsset(AssetSource &source, const std::string_view &name, const Asset &asset) {
    parseAssetID(asset.id.to_string());
    parseAssetID(asset.sourceAsset.to_string());
    asset.validate();

    Bundle bundle;
    bundle.setName(asset.name);
    bundle.setKey("id", asset.id.to_string());
    bundle.setKey("sourceAsset", asset.sourceAsset.to_string());
    bundle.setKey("formatVersion", DERIVED_FORMAT_VERSION);
    bundle.setKey("type", asset.getType());
    asset.write(bundle);

    // Resource IDs are fresh, so a failed write cannot replace a published resource.
    auto bytes = bundle.serialize();
    auto parent = std::filesystem::path(name).parent_path().generic_string();
    if ((!parent.empty() && !source.mkdir(parent)) || !source.store(name, ByteSpan(bytes.data(), bytes.size()))) {
      throw std::runtime_error("Cannot write bundle: " + std::string(name));
    }
  }



  //=========================
  // Import records
  //=========================

  json readImportRecord(AssetSource &source, AssetID sourceAsset) {
    auto name = importRecordPath(sourceAsset);
    auto loaded = source.loadJSON(name);
    if (!loaded) {
      throw std::runtime_error("Cannot read import record: " + name);
    }

    auto record = loaded.unwrap();
    validateRecord(record);
    if (parseAssetID(record.at("sourceAsset")) != sourceAsset) {
      throw std::runtime_error("Import record source ID does not match: " + name);
    }
    return record;
  }


  std::vector<ref<Asset>> readImportedAssets(AssetSource &source, const json &record) {
    validateRecord(record);

    auto sourceAsset = parseAssetID(record.at("sourceAsset"));
    std::vector<ref<Asset>> assets;
    for (const auto &output : record.at("derivedAssets")) {
      auto asset = readDerivedAsset(source, derivedPath(output.at("path").get<std::string>()));
      if (asset->id != parseAssetID(output.at("id")) || asset->sourceAsset != sourceAsset || json(asset->getType()) != output.at("type")) {
        throw std::runtime_error("Derived asset identity or type does not match: " + asset->name);
      }
      assets.push_back(std::move(asset));
    }
    return assets;
  }

}  // namespace ren
