#pragma once

#include <map>
#include <ren/assets/Asset.h>
#include <ren/renderer/shader/ShaderModule.h>
#include <ren/assets/AssetLibrary.h>
#include <ren/assets/AssetSource.h>
#include <ren/assets/AssetStore.h>
#include <ren/core/Option.h>
#include <sol/sol.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

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
    ~AssetManager();
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

    // Timed scans. The --asset-scan flag sets the start value.
    void setScanEnabled(bool enabled) { scanEnabled = enabled; }
    bool isScanEnabled(void) const { return scanEnabled; }

    AssetActivity getActivity(void) const { return activity.load(); }

    // Load the bytes of some asset from the asset source. No Caching!
    bool load(const std::string_view &name, std::vector<u8> &out) { return source.load(name, out); }


    AssetStore &getStore() { return store; }

    // The assets that the sources hold. Call getLibrary().scan() to update it.
    AssetLibrary &getLibrary() { return library; }
    AssetSource &getSource(void) { return source; }


    // Import supported files through importFile, excluding generated bundles and sidecars.
    // Returns imported paths and per-file errors. Each call generates fresh outputs.
    json scanForImports(void);

    // Import one source file from the configured sources. Use its sidecar importer or infer one from its extension.
    // Throws if the importer, settings, source, or writes are invalid.
    json importFile(const std::filesystem::path &path);

   private:
    // Run `work` with ren::backgroundWork as the one task. The caller checked that no task runs.
    void startTask(AssetActivity kind, std::function<void(void)> work);

    // The source that assets are loaded from.
    ren::MultiAssetSource source;
    ren::AssetStore store;
    ren::AssetLibrary library;

    // Background tasks. Only tick() starts a task, so only the worker changes `activity` away
    // from Idle while a task runs. The mutex protects `lastTaskFinished` and lets the destructor
    // wait for Idle.
    std::atomic<AssetActivity> activity{AssetActivity::Idle};
    std::atomic<bool> scanRequested{false};
    bool scanEnabled = true;
    std::mutex taskMutex;
    std::condition_variable taskFinished;
    Option<std::chrono::steady_clock::time_point> lastTaskFinished = None;
  };

  AssetManager &getAssetManager();

  bool loadAssetBytes(const std::string_view &path, std::vector<u8> &out);

}  // namespace ren
