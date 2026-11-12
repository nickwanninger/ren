#include <ren/assets/AssetLibrary.h>
#include <ren/assets/importers/ImportSource.h>
#include <ren/assets/importers/Importer.h>
#include <ren/core/Logging.h>
#include <algorithm>
#include <cctype>
#include <set>

namespace ren {

  AssetLibrary::AssetLibrary(AssetSource &source)
      : source(source) {
    // The scan finds Source files by the extensions of the registered importers.
    registerBuiltinImporters();
  }



  //=========================
  // Scan
  //=========================

  // The engine reads these files directly. Each file holds its ID in the `id` key.
  static bool isNativePath(const std::filesystem::path &path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::tolower(c); });
    return extension == ".scene" || extension == ".prefab";
  }


  static bool sameStat(const AssetStat &a, const AssetStat &b) { return a.type == b.type && a.size == b.size && a.mtime == b.mtime; }


  static AssetStat statFile(AssetSource &source, const std::string &path) {
    AssetStat stat;
    if (!source.stat(path, stat)) {
      throw std::runtime_error("Cannot stat asset file: " + path);
    }
    return stat;
  }


  static AssetID readSidecarID(AssetSource &source, const std::string &sidecarPath) {
    auto sidecar = source.loadJSON(sidecarPath);
    if (!sidecar) {
      throw std::runtime_error("Cannot read sidecar: " + sidecarPath);
    }
    if (sidecar.unwrap().at("version") != SIDECAR_VERSION) {
      throw std::runtime_error("Unsupported sidecar version: " + sidecarPath);
    }
    return parseAssetID(sidecar.unwrap().at("id"));
  }


  void AssetLibrary::scan(void) {
    std::scoped_lock lock(mutex);
    generation++;

    // Gather first, because a visit can write a sidecar. The set removes paths that are in more
    // than one source.
    std::set<std::string> paths;
    bool enumerated = source.enumerate([&](const std::string_view &name) {
      auto parsed = import_detail::trySourcePath(std::string(name));
      if (!parsed) {
        return;
      }
      auto path = parsed.unwrap();
      if (!isDerivedPath(path) && path.extension() != SIDECAR_EXTENSION) {
        paths.insert(path.generic_string());
      }
    });
    if (!enumerated) {
      throw std::runtime_error("Cannot enumerate asset sources for the asset library");
    }

    // One bad file does not stop the scan. Its entry, if any, stays as the last scan left it.
    for (const auto &path : paths) {
      try {
        if (isNativePath(path)) {
          visitNative(path);
        } else if (findImporterForPath(path)) {
          visitSource(path);
        }
      } catch (const std::exception &error) {
        warnln("Asset library cannot scan {}: {}", path, error.what());
        if (auto it = byPath.find(path); it != byPath.end()) {
          markSeen(it->second);
        }
      }
    }

    removeUnseen();
  }


  void AssetLibrary::visitNative(const std::string &path) {
    auto stat = statFile(source, path);
    auto existing = byPath.find(path);
    if (existing != byPath.end() && sameStat(scanState.at(existing->second).fileStat, stat)) {
      markSeen(existing->second);
      return;
    }

    auto file = source.loadJSON(path);
    if (!file) {
      throw std::runtime_error("Cannot read native asset: " + path);
    }

    auto entry = std::make_shared<AssetEntry>();
    entry->id = parseAssetID(file.unwrap().at("id"));
    entry->kind = AssetKind::Native;
    entry->path = path;
    entry->name = std::filesystem::path(path).stem().string();
    addEntry(entry, {.fileStat = stat});
  }


  void AssetLibrary::visitSource(const std::string &path) {
    // A new file gets an identity sidecar. The import fills in the importer and its options later.
    auto sidecarPath = path + std::string(SIDECAR_EXTENSION);
    AssetStat sidecarStat;
    if (!source.stat(sidecarPath, sidecarStat)) {
      json sidecar = {{"version", SIDECAR_VERSION}, {"id", AssetID::generate().to_string()}};
      import_detail::storeJSONAtomic(source, sidecarPath, sidecar, "identity sidecar");
      sidecarStat = statFile(source, sidecarPath);
    }

    // Read the sidecar only when it changed. Otherwise the ID is the one from the last scan.
    auto existing = byPath.find(path);
    bool sidecarChanged = existing == byPath.end() || !sameStat(scanState.at(existing->second).fileStat, sidecarStat);
    AssetID id = sidecarChanged ? readSidecarID(source, sidecarPath) : existing->second;

    // A missing record has a zero stat, so a record that appears or goes away is a change.
    // TODO: read the record and update the Derived entries when the record stat changed.
    AssetStat recordStat;
    bool imported = source.stat(importRecordPath(id), recordStat);
    if (!sidecarChanged && sameStat(scanState.at(id).recordStat, recordStat)) {
      markSeen(id);
      return;
    }

    auto entry = std::make_shared<AssetEntry>();
    entry->id = id;
    entry->kind = AssetKind::Source;
    entry->path = path;
    entry->name = std::filesystem::path(path).filename().string();
    entry->importState = imported ? ImportState::Imported : ImportState::NotImported;
    addEntry(entry, {.fileStat = sidecarStat, .recordStat = recordStat});
  }


  void AssetLibrary::markSeen(AssetID id) { scanState.at(id).seen = generation; }


  // Add or replace the entry for a path. If the path had a different ID, its old entry goes away.
  // If another path has the ID: when that path is gone, the asset moved; when it exists, the
  // copy is a conflict. Throws on a conflict and keeps the existing path.
  void AssetLibrary::addEntry(ref<const AssetEntry> entry, const ScanState &state) {
    if (auto other = entries.find(entry->id); other != entries.end() && other->second->path != entry->path) {
      if (source.exists(other->second->path)) {
        throw std::runtime_error("Asset ID " + entry->id.to_string() + " is also used by " + other->second->path);
      }
      removeEntry(entry->id);
    }
    if (auto old = byPath.find(entry->path); old != byPath.end() && old->second != entry->id) {
      removeEntry(old->second);
    }

    byPath[entry->path] = entry->id;
    scanState[entry->id] = state;
    scanState[entry->id].seen = generation;
    entries[entry->id] = std::move(entry);
  }


  void AssetLibrary::removeEntry(AssetID id) {
    auto it = entries.find(id);
    if (it == entries.end()) {
      return;
    }
    byPath.erase(it->second->path);
    scanState.erase(id);
    entries.erase(it);
  }


  void AssetLibrary::removeUnseen(void) {
    std::vector<AssetID> unseen;
    for (const auto &[id, state] : scanState) {
      if (state.seen != generation) {
        unseen.push_back(id);
      }
    }
    for (auto id : unseen) {
      removeEntry(id);
    }
  }



  //=========================
  // Queries
  //=========================

  ref<const AssetEntry> AssetLibrary::find(AssetID id) const {
    std::scoped_lock lock(mutex);
    auto it = entries.find(id);
    return it != entries.end() ? it->second : nullptr;
  }


  ref<const AssetEntry> AssetLibrary::findByPath(std::string_view path) const {
    std::scoped_lock lock(mutex);
    auto it = byPath.find(std::string(path));
    if (it == byPath.end()) {
      return nullptr;
    }
    return entries.at(it->second);
  }


  void AssetLibrary::forEach(const std::function<void(const ref<const AssetEntry> &)> &fn) const {
    std::scoped_lock lock(mutex);
    for (const auto &[id, entry] : entries) {
      fn(entry);
    }
  }

}  // namespace ren
