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


namespace ren {

  AssetManager &getAssetManager() { return ren::Application::get().getAssetManager(); }

  // The flags must be parsed before the construction, so that they set the start values.
  AssetManager::AssetManager()
      : store(source)
      , library(source)
      , scanEnabled(kAssetScan.get()) {
    registerBuiltinImporters();
  }


  // A task uses the sources and the library, so wait for it before they are destroyed.
  AssetManager::~AssetManager() {
    std::unique_lock lock(taskMutex);
    taskFinished.wait(lock, [&]() { return activity.load() == AssetActivity::Idle; });
  }


  void AssetManager::addFilesystemSource(const std::string_view &path) {
    this->source.addFilesystem(path);
    store.reinit();
  }
  void AssetManager::addEmbeddedSource(void) {
    this->source.addEmbeddedSource();
    store.reinit();
  }
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
      auto interval = std::chrono::milliseconds(kAssetManagerScanIntervalMs.get());
      timedScanReady = !lastTaskFinished || std::chrono::steady_clock::now() - lastTaskFinished.unwrap() >= interval;
    }

    // If there was a requested scan from the application, or a timed scan is ready, start the task.
    if (scanRequested.exchange(false) || timedScanReady) {
      startTask(AssetActivity::Scanning, [this]() { library.scan(); });
    }
  }


  void AssetManager::scheduleScan(void) { scanRequested = true; }


  void AssetManager::startTask(AssetActivity kind, std::function<void(void)> work) {
    // Set the activity
    activity = kind;

    // Fire off the background work.
    ren::backgroundWork([this, work = std::move(work)]() {

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
      std::scoped_lock lock(taskMutex);
      lastTaskFinished = Some(std::chrono::steady_clock::now()); 
      activity = AssetActivity::Idle; // Back to idle.
      taskFinished.notify_all();
    });
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
    bool changed = !sidecar.existed || sidecar.original != settings;
    if (changed) {
      import_detail::storeJSONAtomic(source, sidecar.path, settings, "import settings");
      if (!source.stat(sidecar.path, sidecar.stat)) {
        throw std::runtime_error("Cannot stat import settings: " + sidecar.path);
      }
    }
    record["sidecar"] = sidecar.stat;
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


  json AssetManager::scanForImports(void) {
    REN_PROFILE_FUNCTION();

    // Gather first: import creates files that must not enter this enumeration.
    std::set<std::string> found;
    bool enumerated = source.enumerate([&](const std::string_view &name) {
      auto path = import_detail::sourcePath(std::string(name));
      if (!isDerivedPath(path) && findImporterForPath(path) && source.isFile(path.generic_string())) {
        found.insert(path.generic_string());
      }
    });
    if (!enumerated) {
      throw std::runtime_error("Cannot enumerate asset sources for import");
    }

    // The set removes duplicates from multiple sources. The vector gives the workers indexed access.
    std::vector<std::string> paths(found.begin(), found.end());
    std::mutex mutex;
    json result = {{"imported", json::array()}, {"errors", json::array()}};
    ren::parallel_for(paths.size(), [&](size_t i) {
      const auto &path = paths[i];
      try {
        auto start = std::chrono::high_resolution_clock::now();
        importFile(path);
        auto end = std::chrono::high_resolution_clock::now();
        ren::println("Imported {} in {} ms", path, std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

        std::scoped_lock lock(mutex);
        result["imported"].push_back(path);
      } catch (const std::exception &error) {
        std::scoped_lock lock(mutex);
        result["errors"].push_back({{"path", path}, {"error", error.what()}});
      }
    });
    return result;
  }

}  // namespace ren
