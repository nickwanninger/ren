#include "ImportContext.h"
#include "ImportSource.h"

namespace ren {

  ImportContext::ImportContext(AssetSource &source, AssetID sourceAsset, std::filesystem::path path)
      : source(source)
      , sourceAsset(sourceAsset)
      , sourcePath(std::move(path)) {}


  AssetStat ImportContext::statInput(const std::string &name) {
    AssetStat stat;
    if (!source.stat(name, stat)) {
      throw std::runtime_error("Cannot stat import input: " + name);
    }
    return stat;
  }


  // The first read of an input sets its stat. A later read keeps that stat.
  void ImportContext::trackInput(const std::string &name, const std::string &hash, const AssetStat &stat) {
    std::scoped_lock lock(mutex);
    auto [it, inserted] = inputHashes.emplace(name, hash);
    if (!inserted && it->second != hash) {
      throw std::runtime_error("Input changed during import: " + name);
    }
    inputStats.emplace(name, stat);
  }


  std::vector<u8> ImportContext::readInput(const std::filesystem::path &path) {
    auto name = import_detail::sourcePath(path).generic_string();
    auto stat = statInput(name);
    auto bytes = import_detail::readBytes(source, name);
    trackInput(name, SHA256()(bytes.data(), bytes.size()), stat);
    return bytes;
  }


  ref<AssetFile> ImportContext::openInput(const std::filesystem::path &path) {
    auto name = import_detail::sourcePath(path).generic_string();
    auto stat = statInput(name);
    auto file = source.open(name, AssetOpenFlags::Read);
    if (!file) {
      throw std::runtime_error("Cannot open import input: " + name);
    }
    trackInput(name, import_detail::fileHash(source, name), stat);
    return file;
  }


  AssetID ImportContext::derivedID(AssetType type, std::string_view localName) const {
    // The first 64 bits of the hash. Collisions are not handled.
    auto text = sourceAsset.to_string() + "/" + json(type).get<std::string>() + "/" + std::string(localName);
    auto hash = SHA256()(text);
    return AssetID(std::stoull(hash.substr(0, 16), nullptr, 16));
  }


  AssetID ImportContext::writeDerived(Asset &asset, const std::string &localName) {
    REN_PROFILE_SCOPE("Write Derived Asset");
    asset.id = derivedID(asset.getType(), localName);
    asset.sourceAsset = sourceAsset;
    {
      std::scoped_lock lock(mutex);
      if (!outputIDs.insert(asset.id).second) {
        throw std::runtime_error("Import wrote two outputs with the local name: " + localName);
      }
    }

    // The serialization and the file write run without the lock. Each output has its own path,
    // which the check above makes sure of, and writes to different paths are safe in parallel.
    auto path = derivedBundlePath(asset.id);
    writeDerivedAsset(source, derivedPath(path), asset);

    std::scoped_lock lock(mutex);
    outputs.push_back({{"id", asset.id.to_string()}, {"localName", localName}, {"name", asset.name}, {"type", asset.getType()}, {"path", path}});
    return asset.id;
  }


  void ImportContext::warn(std::string message) {
    std::scoped_lock lock(mutex);
    warnings.push_back(std::move(message));
  }


  void ImportContext::validate() const {
    if (outputs.empty()) {
      throw std::runtime_error("Import produced no supported resources: " + sourcePath.string());
    }

    // An input can change after the importer reads it. Hash each input again before publication.
    for (const auto &[name, hash] : inputHashes) {
      if (import_detail::fileHash(source, name) != hash) {
        throw std::runtime_error("Input changed during import: " + name);
      }
    }
  }

}  // namespace ren
