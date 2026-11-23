#pragma once

#include <map>
#include <ren/assets/Asset.h>
#include <ren/renderer/shader/ShaderModule.h>
#include <ren/assets/AssetLibrary.h>
#include <ren/assets/AssetSource.h>
#include <ren/assets/DerivedAsset.h>
#include <ren/assets/NativeAsset.h>
#include <ren/core/Option.h>
#include <sol/sol.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <unordered_map>

namespace ren {


  // The background task of the AssetManager. At most one task runs at a time.
  enum class AssetActivity : u8 {
    Idle,
    Scanning,
    Importing,
  };


  // The AssetManager is responsible for managing asset importing and retrieval.
  // 
  class AssetManager {
   public:
    AssetManager();
    // Waits for the running task. Call waitForIdle() earlier if a task can use objects that are
    // destroyed first, such as the renderer.
    ~AssetManager();

    // Blocks until no background task runs. Only tick() starts a task, so no new task starts
    // while the caller does not call tick().
    void waitForIdle(void);

    // Add a filesystem source to the asset manager.
    void addFilesystemSource(const std::string_view &path);
    void addEmbeddedSource(void);

    // Add the sources that all ren programs use: the embedded resources,
    // then the asset directory `root`. The store is in `root`, because the
    // embedded source is read-only. Note: this creates `<root>/store` if it
    // does not exist.
    void addDefaultSources(const std::string_view &root = "assets/");

    // The only entry point for background asset work. Starts at most one task when no task runs:
    // a scan when one is scheduled, or when timed scans are on and the scan interval passed since
    // the last task finished. Application calls this one time per frame, from the main thread.
    void tick();

    // Make the next tick start a scan, also when timed scans are off.
    void scheduleScan(void);

    // Make a later tick import these Sources, one at a time, after any scheduled scan.
    void scheduleImport(const std::vector<AssetID> &sources);

    // Timed scans. The --asset-scan flag sets the start value.
    void setScanEnabled(bool enabled) { scanEnabled = enabled; }
    bool isScanEnabled(void) const { return scanEnabled; }

    // The time from the end of one task to the next timed scan. The --asset-scan-interval-ms flag
    // sets the start value.
    void setScanIntervalMs(int milliseconds) { scanIntervalMs = milliseconds; }
    int getScanIntervalMs(void) const { return scanIntervalMs; }

    // When on, a tick imports the pending list without a prompt. The --auto-import flag sets the
    // start value. When off, the app shows the pending list and calls scheduleImport().
    void setAutoImport(bool enabled) { autoImport = enabled; }
    bool isAutoImport(void) const { return autoImport; }

    // When on, a scan hashes the inputs and sidecar of each Stale Source. If they match the record,
    // the record gets the new stats, and the Source is Imported again without an import. The
    // --asset-hash-check flag sets the start value.
    void setHashCheck(bool enabled) { hashCheck = enabled; }
    bool isHashCheck(void) const { return hashCheck.load(); }

    AssetActivity getActivity(void) const { return activity.load(); }

    // The number of tasks that finished. A UI compares it with the value it last saw, to know
    // when to read the library again.
    u64 getFinishedTaskCount(void) const { return finishedTasks.load(); }

    // The duration of the last task, in milliseconds.
    float getLastTaskMilliseconds(void) const { return lastTaskMilliseconds.load(); }

    // The Sources that need an import after the last scan: NotImported and Stale, sorted by path.
    // A dismissed or failed Source is not in the list until its file or sidecar changes.
    std::vector<AssetID> getPendingImports(void) const;

    // The Sources whose last import failed, with the error. They stay here until their file or
    // sidecar changes.
    std::vector<std::pair<AssetID, std::string>> getFailedImports(void) const;

    // Increases each time the pending list changes. A UI compares it with the value it last
    // showed, to know when to show the list again.
    u64 getPendingGeneration(void) const { return pendingGeneration.load(); }

    // Remove Sources from the pending list until their file or sidecar changes, for example when
    // the user answers "not now".
    void dismissImports(const std::vector<AssetID> &sources);

    // Load the bytes of some asset from the asset source. No Caching!
    bool load(const std::string_view &name, std::vector<u8> &out) { return source.load(name, out); }


    // The assets that the sources hold. Call getLibrary().scan() to update it.
    AssetLibrary &getLibrary() { return library; }
    AssetSource &getSource(void) { return source; }


    // Import one source file from the configured sources. Use its sidecar importer or infer one from its extension.
    // Throws if the importer, settings, source, or writes are invalid.
    json importFile(const std::filesystem::path &path);


    // The runtime object of an asset. The first call loads it, and later calls return the same
    // object, so the AssetManager keeps every resolved object. Call on the main thread, because a
    // load can make GPU objects. Returns None for the null ID, an asset of another type, an ID that
    // the library does not have, or a failed load. A failed load warns one time and is not tried again.
    template <typename T>
      requires std::derived_from<T, RuntimeAsset>
    Option<ref<T>> resolve(AssetID id);

    // Drop every resolved asset and cached failure. Runtime assets own GPU objects, so the
    // application calls this before it destroys the renderer. Objects that other code still holds
    // stay alive until that code drops them.
    void releaseRuntimeAssets(void);

   private:
    // Run `work` with ren::backgroundWork as the one task. The caller checked that no task runs.
    void startTask(AssetActivity kind, std::function<void(void)> work);

    // The stats of a Source file and its sidecar. A dismissed or failed Source comes back to the
    // pending list when they change. Only these two files are checked, not other inputs.
    struct SourceStats {
      AssetStat file;
      AssetStat sidecar;

      bool operator==(const SourceStats &) const = default;
    };

    // An import that failed, with the stats of its Source at the time.
    struct FailedImport {
      SourceStats stats;
      std::string error;
    };

    // Read the stats of a Source and its sidecar. Missing files give zero stats.
    SourceStats statSource(const std::string &path);

    // Make the pending list from the library. The scan task calls this after the scan.
    void updatePendingImports(void);

    // The import task. Imports each Source and updates its library entries. A failure goes to
    // failedImports. Each Source leaves the pending list when its import ends.
    void importSources(const std::vector<AssetID> &sources);

    // The hash check of the scan task. Runs outside the library lock.
    void confirmStaleSources(void);

    // If the inputs and sidecar of a Source have the hashes in its record, write the current
    // stats into the record and return true. Returns false if anything changed or is missing.
    bool refreshUnchangedRecord(const AssetEntry &entry);

    // The cached object of `id`. On a miss, `load` makes the object from the library entry. Returns
    // null if the library entry has another type than `type`, or the load failed.
    ref<RuntimeAsset> findRuntimeAsset(AssetID id, AssetType type, const std::function<ref<RuntimeAsset>(const AssetEntry &)> &load);

    // The source that assets are loaded from.
    ren::MultiAssetSource source;
    ren::AssetLibrary library;

    // Background tasks. Only tick() starts a task, so only the worker changes `activity` away
    // from Idle while a task runs. The mutex protects `lastTaskFinished` and lets the destructor
    // wait for Idle.
    std::atomic<AssetActivity> activity{AssetActivity::Idle};
    std::atomic<bool> scanRequested{false};
    bool scanEnabled = true;
    bool autoImport = true;
    int scanIntervalMs = 1000;
    // The scan task reads it on a worker thread.
    std::atomic<bool> hashCheck{true};
    std::atomic<u64> finishedTasks{0};
    std::atomic<float> lastTaskMilliseconds{0};
    // Sources that scheduleImport() asked for. The main thread writes and reads it in tick().
    std::vector<AssetID> importRequests;
    std::mutex taskMutex;
    std::condition_variable taskFinished;
    Option<std::chrono::steady_clock::time_point> lastTaskFinished = None;

    // Resolved runtime assets by ID. A failed load is null, so that it is not tried again. Only the
    // main thread uses it.
    std::map<AssetID, ref<RuntimeAsset>> runtimeAssets;

    // The pending list. The scan task writes it, and the main thread reads and dismisses.
    mutable std::mutex pendingMutex;
    std::vector<AssetID> pendingImports;
    std::unordered_map<AssetID, SourceStats> dismissedImports;
    std::unordered_map<AssetID, FailedImport> failedImports;
    std::atomic<u64> pendingGeneration{0};
  };

  AssetManager &getAssetManager();


  // The asset type that a runtime type T loads from: T::TYPE for a native type, or the type of
  // T::SourceAsset.
  template <typename T>
  constexpr AssetType runtimeAssetType(void) {
    if constexpr (std::derived_from<T, NativeAsset>) {
      return T::TYPE;
    } else {
      return T::SourceAsset::TYPE;
    }
  }


  template <typename T>
    requires std::derived_from<T, RuntimeAsset>
  Option<ref<T>> AssetManager::resolve(AssetID id) {
    if (id == UUID::null) {
      return None;
    }

    auto load = [this](const AssetEntry &entry) -> ref<RuntimeAsset> {
      if constexpr (std::derived_from<T, NativeAsset>) {
        // A native file holds the file JSON. A derived bundle holds the content.
        if (entry.kind == AssetKind::Native) {
          auto file = source.loadJSON(entry.path);
          if (!file) {
            throw std::runtime_error("Cannot read " + entry.path);
          }
          return deserializeNativeAsset<T>(file.unwrap());
        }
        auto content = std::dynamic_pointer_cast<NativeContentAsset>(readDerivedAsset(source, entry.path));
        if (!content) {
          throw std::runtime_error("Bundle has no native content: " + entry.path);
        }
        return deserializeNativeAsset<T>(content->serializeNativeFile());
      } else {
        auto asset = readDerivedAsset(source, entry.path);
        const auto *typed = dynamic_cast<const typename T::SourceAsset *>(asset.get());
        if (!typed) {
          throw std::runtime_error("Bundle has another asset class: " + entry.path);
        }
        return T::fromAsset(*typed);
      }
    };

    auto asset = findRuntimeAsset(id, runtimeAssetType<T>(), load);
    if (!asset) {
      return None;
    }

    // Two runtime types can load from the same asset type, and the cache keeps the object of the
    // first one that resolved the ID.
    auto typed = std::dynamic_pointer_cast<T>(asset);
    if (!typed) {
      ren::warnln("Asset {} was resolved as another runtime type", id.to_string());
      return None;
    }
    return Some(typed);
  }

  bool loadAssetBytes(const std::string_view &path, std::vector<u8> &out);

}  // namespace ren
