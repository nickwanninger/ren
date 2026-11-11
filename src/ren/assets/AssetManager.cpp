#include <ren/assets/AssetManager.h>

#include <ren/renderer/Texture.h>
#include <ren/renderer/shader/ShaderModule.h>
#include <ren/core/Application.h>
#include <imgui/imgui.h>
#include <ren/misc/hash.h>
#include <ren/core/UUID.h>
#include <algorithm>
#include <cctype>
#include <set>
#include "ren/core/ThreadPool.h"
#include <ren/assets/importers/ImportSource.h>
#include <ren/assets/importers/Importer.h>
#include <ren/core/Bundle.h>


namespace ren {

  AssetManager &getAssetManager() { return ren::Application::get().getAssetManager(); }

  AssetManager::AssetManager()
      : store(source) {
    registerBuiltinImporters();
  }

  AssetManager::~AssetManager() {}


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


  void AssetManager::tick() { REN_PROFILE_FUNCTION(); }

  bool loadAssetBytes(const std::string_view &path, std::vector<u8> &out) { return getAssetManager().load(path, out); }



  //=========================
  // Import
  //=========================

  // Increase these when a change makes earlier outputs or sidecars invalid.
  static constexpr int IMPORTER_VERSION = 1;
  static constexpr int SIDECAR_VERSION = 1;


  // The sidecar as it is on disk, and the settings that the import will save.
  struct Sidecar {
    std::string path;
    bool existed = false;
    std::vector<u8> originalBytes;
    json original;
    json settings;
  };


  // Read the sidecar, or make one with a new identity. This does not write it.
  static Sidecar loadSidecar(AssetSource &source, const std::filesystem::path &path) {
    Sidecar sidecar;
    sidecar.path = path.generic_string() + ".import";
    sidecar.existed = source.exists(sidecar.path);
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
    json dependencies = json::array();
    for (const auto &[name, hash] : context.getInputHashes()) {
      dependencies.push_back({{"path", name}, {"hash", hash}});
    }

    // The fingerprint changes when any input, setting, or format version changes.
    json fingerprint = {{"dependencies", dependencies},
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

    // The importer writes bundles with fresh IDs, so a failure here leaves the published record valid.
    ImportContext context(source, id, path);
    importer->import(context);
    context.validate();
    auto record = makeImportRecord(context, settings, importerName);


    // Save the sidecar, then publish the record. If publication fails, restore the old sidecar, so that a
    // new identity or new defaults do not stay without a record.
    bool changed = !sidecar.existed || sidecar.original != settings;
    if (changed) {
      import_detail::storeJSONAtomic(source, sidecar.path, settings, "import settings");
    }
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
