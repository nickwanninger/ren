#include "ImportContext.h"
#include "ImportSource.h"

namespace ren {

  ImportContext::ImportContext(AssetSource &source, AssetID sourceAsset, std::filesystem::path path)
      : source(source)
      , sourceAsset(sourceAsset)
      , sourcePath(std::move(path)) {}


  void ImportContext::trackInput(const std::string &name, const std::string &hash) {
    std::scoped_lock lock(mutex);
    auto [it, inserted] = inputHashes.emplace(name, hash);
    if (!inserted && it->second != hash) {
      throw std::runtime_error("Input changed during import: " + name);
    }
  }


  std::vector<u8> ImportContext::readInput(const std::filesystem::path &path) {
    auto name = import_detail::sourcePath(path).generic_string();
    auto bytes = import_detail::readBytes(source, name);
    trackInput(name, SHA256()(bytes.data(), bytes.size()));
    return bytes;
  }


  ref<AssetFile> ImportContext::openInput(const std::filesystem::path &path) {
    auto name = import_detail::sourcePath(path).generic_string();
    auto file = source.open(name, AssetOpenFlags::Read);
    if (!file) {
      throw std::runtime_error("Cannot open import input: " + name);
    }
    trackInput(name, import_detail::fileHash(source, name));
    return file;
  }


  AssetID ImportContext::derivedID(AssetType type, std::string_view localName) const {
    // The first 64 bits of the hash. Collisions are not handled.
    auto text = sourceAsset.to_string() + "/" + json(type).get<std::string>() + "/" + std::string(localName);
    auto hash = SHA256()(text);
    return AssetID(std::stoull(hash.substr(0, 16), nullptr, 16));
  }


  AssetID ImportContext::writeDerived(Asset &asset, const std::string &localName) {
    // TODO: Are asset sources safe for parallel writes? Until then, the lock also serializes the bundle writes.
    std::scoped_lock lock(mutex);
    asset.id = derivedID(asset.getType(), localName);
    asset.sourceAsset = sourceAsset;
    if (!outputIDs.insert(asset.id).second) {
      throw std::runtime_error("Import wrote two outputs with the local name: " + localName);
    }

    auto path = derivedBundlePath(asset.id);
    writeDerivedAsset(source, derivedPath(path), asset);
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
