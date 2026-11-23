#include <ren/assets/AssetManager.h>

#include <ren/renderer/Texture.h>
#include <ren/renderer/shader/ShaderModule.h>
#include <ren/core/Application.h>
#include <imgui/imgui.h>
#include <ren/misc/hash.h>
#include <ren/core/UUID.h>
#include <set>
#include "ren/core/ThreadPool.h"
#include <ren/assets/importers/ImportSource.h>
#include <ren/assets/importers/Importer.h>
#include <ren/core/Bundle.h>
#include <ren/core/Flag.h>
#include <ren/core/Logging.h>


// Launch options can disable timed scans. This will be something we need to
// deal with when a game is running in runtime mode, as we won't run any
// importing logic then. (It would be an error, if we eventually have proper
// game packaging eventually.)
static ren::Flag<bool> kAssetScan("asset-scan", true, "Scan the asset sources on a timer. Scans that the program schedules still run.");
static ren::Flag<int> kAssetManagerScanIntervalMs("asset-scan-interval-ms", 1000, "Time from the end of one asset task to the next timed scan.");
static ren::Flag<bool> kAutoImport("auto-import", true, "Import new and changed asset sources after a scan, without a prompt.");
static ren::Flag<bool> kAssetHashCheck("asset-hash-check", true,
                                       "After a scan, hash stale asset sources, and do not import the ones whose content is the same.");


namespace ren {

  AssetManager &getAssetManager() { return ren::Application::get().getAssetManager(); }

  // The flags must be parsed before the construction, so that they set the start values.
  AssetManager::AssetManager()
      : library(source)
      , scanEnabled(kAssetScan.get())
      , autoImport(kAutoImport.get())
      , scanIntervalMs(kAssetManagerScanIntervalMs.get())
      , hashCheck(kAssetHashCheck.get()) {
    registerBuiltinImporters();
  }


  // A task uses the sources and the library, so wait for it before they are destroyed.
  AssetManager::~AssetManager() { waitForIdle(); }


  void AssetManager::waitForIdle(void) {
    std::unique_lock lock(taskMutex);
    taskFinished.wait(lock, [&]() { return activity.load() == AssetActivity::Idle; });
  }


  void AssetManager::addFilesystemSource(const std::string_view &path) { this->source.addFilesystem(path); }

  void AssetManager::addEmbeddedSource(void) { this->source.addEmbeddedSource(); }

  void AssetManager::addDefaultSources(const std::string_view &root) {
    addEmbeddedSource();
    addFilesystemSource(root);
  }


  //=========================
  // Background tasks
  //=========================

  void AssetManager::tick() {
    REN_PROFILE_SCOPE("AssetManager::tick");


    // if the manager is not idle, wait for the next tick.
    if (activity.load() != AssetActivity::Idle) {
      return;
    }

    // We can scan every once in a while (a timed scan). This can be disabled
    // with --asset-scan, but a scheduled scan still runs.
    bool timedScanReady = false;
    if (scanEnabled) {
      // Check the last task finished time under the lock, so that the destructor sees a consistent state.
      std::scoped_lock lock(taskMutex);
      auto interval = std::chrono::milliseconds(scanIntervalMs);
      timedScanReady = !lastTaskFinished || std::chrono::steady_clock::now() - lastTaskFinished.unwrap() >= interval;
    }

    // If there was a requested scan from the application, or a timed scan is ready, start the task.
    if (scanRequested.exchange(false) || timedScanReady) {
      startTask(AssetActivity::Scanning, [this]() {
        library.scan();
        if (hashCheck) {
          confirmStaleSources();
        }
        updatePendingImports();
      });
      return;
    }

    // With auto-import, the pending list is the request. The import task removes each Source from
    // the list, so the next tick does not import it again.
    if (autoImport && importRequests.empty()) {
      importRequests = getPendingImports();
    }
    if (!importRequests.empty()) {
      startTask(AssetActivity::Importing, [this, sources = std::move(importRequests)]() { importSources(sources); });
      importRequests.clear();
    }
  }


  void AssetManager::scheduleScan(void) { scanRequested = true; }


  void AssetManager::scheduleImport(const std::vector<AssetID> &sources) {
    for (auto id : sources) {
      if (std::find(importRequests.begin(), importRequests.end(), id) == importRequests.end()) {
        importRequests.push_back(id);
      }
    }
  }


  void AssetManager::importSources(const std::vector<AssetID> &sources) {
    for (auto id : sources) {
      auto entry = library.find(id);
      if (!entry || entry->kind != AssetKind::Source) {
        continue;
      }

      // Stat before the import, so that an edit during a failed import brings the Source back.
      auto stats = statSource(entry->path);
      Option<std::string> error = None;

      auto start = std::chrono::steady_clock::now();
      try {
        importFile(entry->path);
        library.rescan(entry->path);
      } catch (const std::exception &failure) {
        error = Some(std::string(failure.what()));
        ren::warnln("Cannot import {}: {}", entry->path, failure.what());
      }
      auto end = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration<float, std::milli>(end - start).count();
      // ren::dbgln("Imported {} ({}) in {:.2f} ms", entry->path, entry->id.to_string(), duration);
      ren::dbgln("{:8.2f} ms import of {} ({})", duration, entry->id.to_string(), entry->path);

      std::scoped_lock lock(pendingMutex);
      std::erase(pendingImports, id);
      if (error) {
        failedImports[id] = {stats, error.unwrap()};
      } else {
        failedImports.erase(id);
      }
      pendingGeneration++;
    }
  }


  void AssetManager::confirmStaleSources(void) {
    std::vector<ref<const AssetEntry>> stale;
    library.forEach([&](const ref<const AssetEntry> &entry) {
      if (entry->importState == ImportState::Stale) {
        stale.push_back(entry);
      }
    });

    // The library lock is not held here, so the hashes do not block readers. One Source that
    // cannot be checked stays Stale, and the others still get their check.
    for (const auto &entry : stale) {
      try {
        if (refreshUnchangedRecord(*entry)) {
          library.rescan(entry->path);
        }
      } catch (const std::exception &error) {
        ren::warnln("Cannot hash-check {}: {}", entry->path, error.what());
      }
    }
  }


  bool AssetManager::refreshUnchangedRecord(const AssetEntry &entry) {
    REN_PROFILE_SCOPE("Refresh Unchanged Record");
    auto record = readImportRecord(source, entry.id);

    // Stat each file before its hash, so that an edit during the hash makes the next scan see a
    // change.
    auto sidecarPath = entry.path + std::string(SIDECAR_EXTENSION);
    AssetStat sidecarStat;
    if (!source.stat(sidecarPath, sidecarStat)) {
      return false;
    }
    bool sidecarSame = record.contains("sidecar") && record.at("sidecar").get<AssetStat>() == sidecarStat;
    if (!sidecarSame && (!record.contains("sidecarHash") || import_detail::fileHash(source, sidecarPath) != record.at("sidecarHash"))) {
      return false;
    }

    for (auto &dependency : record.at("dependencies")) {
      auto path = dependency.at("path").get<std::string>();
      AssetStat stat;
      if (!source.stat(path, stat) || import_detail::fileHash(source, path) != dependency.at("hash")) {
        return false;
      }
      dependency["stat"] = stat;
    }

    // The content is the same, so only the stats in the record change.
    record["sidecar"] = sidecarStat;
    import_detail::publishRecord(source, entry.id, record);
    return true;
  }


  void AssetManager::startTask(AssetActivity kind, std::function<void(void)> work) {
    // Set the activity
    activity = kind;

    // Fire off the background work.
    ren::backgroundWork([this, work = std::move(work)]() {
      auto start = std::chrono::steady_clock::now();

      // The work can throw, but the backgroundWork cannot handle it. Catch it
      // here and log it, so that the task can finish and the manager can return
      // to Idle.
      try {
        work();
      } catch (const std::exception &error) {
        // TODO: we need some proper way to report this out to the user, but just log it for now.
        ren::warnln("Asset task failed: {}", error.what());
      }

      // Notify the background task is finished, so the next tick is no longer idle.
      auto end = std::chrono::steady_clock::now();
      std::scoped_lock lock(taskMutex);
      lastTaskMilliseconds = std::chrono::duration<float, std::milli>(end - start).count();
      finishedTasks++;
      lastTaskFinished = Some(end);
      activity = AssetActivity::Idle;  // Back to idle.
      taskFinished.notify_all();
    });
  }




  //=========================
  // Pending imports
  //=========================

  std::vector<AssetID> AssetManager::getPendingImports(void) const {
    std::scoped_lock lock(pendingMutex);
    return pendingImports;
  }


  std::vector<std::pair<AssetID, std::string>> AssetManager::getFailedImports(void) const {
    std::scoped_lock lock(pendingMutex);
    std::vector<std::pair<AssetID, std::string>> failures;
    for (const auto &[id, failed] : failedImports) {
      failures.emplace_back(id, failed.error);
    }
    return failures;
  }


  AssetManager::SourceStats AssetManager::statSource(const std::string &path) {
    SourceStats stats;
    source.stat(path, stats.file);
    source.stat(path + std::string(SIDECAR_EXTENSION), stats.sidecar);
    return stats;
  }


  void AssetManager::dismissImports(const std::vector<AssetID> &sources) {
    std::scoped_lock lock(pendingMutex);
    for (auto id : sources) {
      auto entry = library.find(id);
      if (entry) {
        dismissedImports[id] = statSource(entry->path);
      }
      std::erase(pendingImports, id);
    }
    pendingGeneration++;
  }


  void AssetManager::updatePendingImports(void) {
    std::vector<ref<const AssetEntry>> needed;
    library.forEach([&](const ref<const AssetEntry> &entry) {
      if (entry->importState == ImportState::NotImported || entry->importState == ImportState::Stale) {
        needed.push_back(entry);
      }
    });
    std::sort(needed.begin(), needed.end(), [](const auto &a, const auto &b) { return a->path < b->path; });

    std::scoped_lock lock(pendingMutex);

    // A dismissed or failed Source stays out until its file or sidecar changes. A Source that
    // changed, or no longer needs an import, leaves both maps, so a later change asks again.
    std::unordered_map<AssetID, SourceStats> dismissed;
    std::unordered_map<AssetID, FailedImport> failed;
    std::vector<AssetID> pending;
    for (const auto &entry : needed) {
      auto wasDismissed = dismissedImports.find(entry->id);
      auto wasFailed = failedImports.find(entry->id);
      if (wasDismissed == dismissedImports.end() && wasFailed == failedImports.end()) {
        pending.push_back(entry->id);
        continue;
      }

      auto stats = statSource(entry->path);
      bool held = false;
      if (wasDismissed != dismissedImports.end() && wasDismissed->second == stats) {
        dismissed.emplace(entry->id, wasDismissed->second);
        held = true;
      }
      if (wasFailed != failedImports.end() && wasFailed->second.stats == stats) {
        failed.emplace(entry->id, wasFailed->second);
        held = true;
      }
      if (!held) {
        pending.push_back(entry->id);
      }
    }

    dismissedImports = std::move(dismissed);
    failedImports = std::move(failed);
    if (pending != pendingImports) {
      pendingImports = std::move(pending);
      pendingGeneration++;
    }
  }


  bool loadAssetBytes(const std::string_view &path, std::vector<u8> &out) { return getAssetManager().load(path, out); }



  //=========================
  // Import
  //=========================

  // Increase this when a change makes earlier outputs invalid.
  static constexpr int IMPORTER_VERSION = 1;


  // The sidecar as it is on disk, and the settings that the import will save.
  struct Sidecar {
    std::string path;
    bool existed = false;
    std::vector<u8> originalBytes;
    json original;
    json settings;
    // The stat before the read, or after the write. An edit during the import makes the source stale.
    AssetStat stat;
  };


  // Read the sidecar, or make one with a new identity. This does not write it.
  static Sidecar loadSidecar(AssetSource &source, const std::filesystem::path &path) {
    Sidecar sidecar;
    sidecar.path = path.generic_string() + ".import";
    sidecar.existed = source.stat(sidecar.path, sidecar.stat);
    if (sidecar.existed) {
      sidecar.originalBytes = import_detail::readBytes(source, sidecar.path);
      auto loaded = source.loadJSON(sidecar.path);
      if (!loaded) {
        throw std::runtime_error("Cannot read import settings: " + sidecar.path);
      }
      sidecar.original = loaded.unwrap();
    } else {
      sidecar.original = {{"version", SIDECAR_VERSION}, {"id", AssetID::generate().to_string()}};
    }

    sidecar.settings = sidecar.original;
    if (sidecar.settings.at("version") != SIDECAR_VERSION) {
      throw std::runtime_error("Invalid import identity: " + sidecar.path);
    }
    return sidecar;
  }


  // Undo a sidecar write after a failed publication.
  static void restoreSidecar(AssetSource &source, Sidecar &sidecar) {
    bool restored = sidecar.existed ? source.store(sidecar.path, ByteSpan(sidecar.originalBytes.data(), sidecar.originalBytes.size()))
                                    : source.remove(sidecar.path);
    if (!restored) {
      throw std::runtime_error("Import publication failed and settings restoration failed: " + sidecar.path);
    }
  }


  static json makeImportRecord(const ImportContext &context, const json &settings, const std::string &importer) {
    // The fingerprint uses only the hashes, so that a touched but unchanged input keeps it.
    json hashes = json::array();
    json dependencies = json::array();
    for (const auto &[name, hash] : context.getInputHashes()) {
      hashes.push_back({{"path", name}, {"hash", hash}});
      dependencies.push_back({{"path", name}, {"hash", hash}, {"stat", context.getInputStats().at(name)}});
    }

    // The fingerprint changes when any input, setting, or format version changes.
    json fingerprint = {{"dependencies", hashes},
                        {"settings", settings},
                        {"importerVersion", IMPORTER_VERSION},
                        {"assetFormatVersion", DERIVED_FORMAT_VERSION},
                        {"bundleVersion", Bundle::VERSION}};
    auto fingerprintText = fingerprint.dump();
    auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    auto sourcePath = context.sourcePath.generic_string();
    return {{"version", IMPORT_RECORD_VERSION},
            {"sourceAsset", context.sourceAsset.to_string()},
            {"sourcePath", sourcePath},
            {"sourceHash", context.getInputHashes().at(sourcePath)},
            {"importer", importer},
            {"importerVersion", IMPORTER_VERSION},
            {"dependencies", dependencies},
            {"fingerprint", SHA256()(fingerprintText.data(), fingerprintText.size())},
            {"importedAt", now},
            {"derivedAssets", context.getOutputs()},
            {"warnings", context.getWarnings()}};
  }


  json AssetManager::importFile(const std::filesystem::path &sourcePath) {
    REN_PROFILE_SCOPE("ImportFile");

    auto path = import_detail::sourcePath(sourcePath);
    if (isDerivedPath(path) || !source.isFile(path.generic_string())) {
      throw std::runtime_error("Cannot import generated or missing asset: " + path.string());
    }


    // Fill the settings. The sidecar stays unchanged until the import succeeds.
    auto sidecar = loadSidecar(source, path);
    auto &settings = sidecar.settings;
    auto id = parseAssetID(settings.at("id"));
    if (!settings.contains("importer")) {
      const auto *inferred = findImporterForPath(path);
      settings["importer"] = inferred ? inferred->name : "";
    }
    auto importerName = settings.at("importer").get<std::string>();
    const auto *info = findImporter(importerName);
    if (!info) {
      throw std::runtime_error("Unsupported importer for " + path.string() + ": " + importerName);
    }

    if (!settings.contains("options")) {
      settings["options"] = json::object();
    }
    if (!settings.at("options").is_object()) {
      throw std::runtime_error("Import options must be an object: " + sidecar.path);
    }


    // Construct the importer. This validates the options and fills the missing defaults into the settings.
    auto importer = info->create(settings.at("options"));

    // A failure here leaves the published record. Bundles that the import already replaced keep their IDs.
    ImportContext context(source, id, path);
    importer->import(context);
    context.validate();
    auto record = makeImportRecord(context, settings, importerName);


    // Save the sidecar, then publish the record. If publication fails, restore the old sidecar, so that a
    // new identity or new defaults do not stay without a record.
    // The record keeps the sidecar's stat and hash. The hash check after a scan uses the hash to
    // tell a touched sidecar from changed settings.
    bool changed = !sidecar.existed || sidecar.original != settings;
    std::string sidecarHash;
    if (changed) {
      import_detail::storeJSONAtomic(source, sidecar.path, settings, "import settings");
      if (!source.stat(sidecar.path, sidecar.stat)) {
        throw std::runtime_error("Cannot stat import settings: " + sidecar.path);
      }
      sidecarHash = import_detail::fileHash(source, sidecar.path);
    } else {
      sidecarHash = SHA256()(sidecar.originalBytes.data(), sidecar.originalBytes.size());
    }
    record["sidecar"] = sidecar.stat;
    record["sidecarHash"] = sidecarHash;
    try {
      import_detail::publishRecord(source, id, record);
    } catch (...) {
      if (changed) {
        restoreSidecar(source, sidecar);
      }
      throw;
    }
    return record;
  }

}  // namespace ren
