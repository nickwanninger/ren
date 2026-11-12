#pragma once

#include <ren/assets/Asset.h>
#include <ren/assets/AssetSource.h>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

namespace ren {

  // How an asset comes into the library.
  enum class AssetKind : u8 {
    // A file that an importer reads, such as a GLB or a PNG. Its `.import` sidecar holds its ID.
    Source,
    // A file that the engine reads directly, such as a scene. The file holds its ID.
    Native,
    // An output of an import, in the derived folder. The import record of its Source lists it.
    Derived,
  };


  // The import state of a Source on disk. Native and Derived entries use `None`.
  enum class ImportState : u8 {
    None,
    NotImported,
    Imported,
    // A dependency or the sidecar has a different stat than at the last import, or is missing.
    // The content can still be the same, for example after a touch.
    Stale,
  };


  // A description of one asset. An entry does not change after the library makes it.
  // An update makes a new entry, so a caller that holds an entry keeps a consistent version.
  struct AssetEntry {
    AssetID id;
    AssetKind kind = AssetKind::Source;
    // Unknown for a Source, because one import can make assets of different types.
    AssetType type = AssetType::Unknown;
    // The path relative to the source root: the file for a Source or Native, the bundle for a Derived.
    std::string path;
    std::string name;
    // The Source that owns a Derived entry. Null for other kinds.
    AssetID source = AssetID(UUID::null);
    ImportState importState = ImportState::None;
  };



  // The AssetLibrary knows which assets exist. It maps each AssetID to an entry. It does not
  // load or import assets. Call scan() to update the map from the source. All functions are
  // thread-safe.
  class AssetLibrary {
   public:
    explicit AssetLibrary(AssetSource &source);

    // Update the map from the source in place. Writes an identity sidecar for each importable
    // file that has none. Blocks until the scan is done.
    void scan(void);

    // Returns null if no entry has the ID or the path.
    ref<const AssetEntry> find(AssetID id) const;
    ref<const AssetEntry> findByPath(std::string_view path) const;

    // Call `fn` for each entry, in no defined order. The library is locked during the
    // calls, so `fn` must not call the library.
    void forEach(const std::function<void(const ref<const AssetEntry> &)> &fn) const;

   private:
    // What the scan last read for an entry. The scan reads a file again only when its stat
    // changed. A Derived entry uses only `seen`, because its Source reads the record.
    struct ScanState {
      // The last scan generation that visited the entry. The scan removes older entries.
      u64 seen = 0;
      // The Native file, or the sidecar of a Source.
      AssetStat fileStat;
      // The import record of a Source.
      AssetStat recordStat;
      // The Derived entries that the record of a Source lists.
      std::vector<AssetID> outputs;

      // The stats that the record of a Source stores. False if the record has none, which makes
      // the Source stale.
      bool hasImportStats = false;
      AssetStat importedSidecar;
      std::vector<std::pair<std::string, AssetStat>> importedInputs;
    };

    // Scan steps. The caller holds the lock. Each visit throws if a file cannot be read or written.
    void visitNative(const std::string &path);
    void visitSource(const std::string &path);
    void updateImportState(AssetID id, const AssetStat &sidecarStat);
    void markSeen(AssetID id);
    void addEntry(ref<const AssetEntry> entry, const ScanState &state);
    void removeEntry(AssetID id);
    void removeUnseen(void);

    AssetSource &source;

    // Protects all members below.
    mutable std::mutex mutex;
    std::unordered_map<AssetID, ref<const AssetEntry>> entries;
    std::unordered_map<std::string, AssetID> byPath;
    std::unordered_map<AssetID, ScanState> scanState;
    u64 generation = 0;
  };

}  // namespace ren
