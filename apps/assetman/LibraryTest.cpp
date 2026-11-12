#include "LibraryTest.h"
#include <ren/assets/AssetLibrary.h>
#include <ren/assets/AssetManager.h>
#include <ren/assets/DerivedAsset.h>
#include <ren/core/Logging.h>
#include <fstream>
#include <thread>


namespace {

  void verify(bool condition, const std::string &message) {
    if (!condition) {
      throw std::runtime_error(message);
    }
  }


  struct TemporaryAssets {
    std::filesystem::path root = std::filesystem::temp_directory_path() / ("ren-library-test-" + ren::AssetID::generate().to_string());

    TemporaryAssets() { std::filesystem::create_directories(root); }

    ~TemporaryAssets() {
      std::error_code error;
      std::filesystem::remove_all(root, error);
    }
  };


  // Write a file and move its mtime forward, so that a rewrite in the same clock tick is a change.
  void writeText(const std::filesystem::path &path, const std::string &text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::trunc) << text;
    static auto offset = std::chrono::seconds(0);
    offset += std::chrono::seconds(1);
    std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now() + offset);
  }


  json readJSON(const std::filesystem::path &path) {
    std::ifstream input(path);
    return json::parse(input);
  }


  ren::AssetStat statOf(ren::AssetSource &source, const std::string &path) {
    ren::AssetStat stat;
    verify(source.stat(path, stat), "Cannot stat fixture: " + path);
    return stat;
  }


  // Write an import record of `path` that lists one Texture output for each ID. With `withStats`,
  // the record stores the current stats of the file and its sidecar, as an import does.
  void writeRecord(ren::AssetSource &source, const std::filesystem::path &root, const std::string &path, ren::AssetID sourceAsset,
                   const std::vector<ren::AssetID> &outputs, bool withStats = true) {
    json derived = json::array();
    for (auto id : outputs) {
      derived.push_back({{"id", id.to_string()}, {"name", "out-" + id.to_string()}, {"path", ren::derivedBundlePath(id)}, {"type", "Texture"}});
    }
    json record = {{"version", ren::IMPORT_RECORD_VERSION}, {"sourceAsset", sourceAsset.to_string()}, {"derivedAssets", derived}};
    if (withStats) {
      record["dependencies"] = json::array({{{"path", path}, {"hash", ""}, {"stat", statOf(source, path)}}});
      record["sidecar"] = statOf(source, path + ".import");
    } else {
      record["dependencies"] = json::array({{{"path", path}, {"hash", ""}}});
    }
    writeText(root / ren::importRecordPath(sourceAsset), record.dump());
  }


  size_t countEntries(const ren::AssetLibrary &library) {
    size_t count = 0;
    library.forEach([&](const ren::ref<const ren::AssetEntry> &) { count++; });
    return count;
  }


  // Tick until the manager is idle again. Fails after about one second.
  void tickUntilIdle(ren::AssetManager &manager) {
    for (int i = 0; i < 1000 && manager.getActivity() != ren::AssetActivity::Idle; i++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    verify(manager.getActivity() == ren::AssetActivity::Idle, "Asset task did not finish");
  }


  // tick() starts the background scans. This test assumes the default --asset-scan-interval-ms,
  // which is much longer than the test.
  void verifyManagerTasks(void) {
    TemporaryAssets temporary;
    writeText(temporary.root / "first.png", "not decoded by the scan");

    // Auto-import stays off until the last part, so that each tick starts only the expected task.
    ren::AssetManager manager;
    manager.addFilesystemSource(temporary.root.string());
    manager.setAutoImport(false);
    auto count = [&]() { return countEntries(manager.getLibrary()); };

    // The first tick scans at once. The next tick waits for the interval.
    manager.tick();
    tickUntilIdle(manager);
    verify(count() == 1, "First tick did not scan");
    writeText(temporary.root / "second.png", "not decoded by the scan");
    manager.tick();
    verify(manager.getActivity() == ren::AssetActivity::Idle && count() == 1, "Tick scanned before the interval");

    // A scheduled scan runs on the next tick, also with timed scans off.
    manager.setScanEnabled(false);
    manager.scheduleScan();
    manager.tick();
    tickUntilIdle(manager);
    verify(count() == 2, "Scheduled scan did not run");

    // Both files are not imported, so both are pending, sorted by path.
    auto first = manager.getLibrary().findByPath("first.png")->id;
    auto second = manager.getLibrary().findByPath("second.png")->id;
    verify(manager.getPendingImports() == std::vector<ren::AssetID>({first, second}), "Pending list is wrong");
    auto generation = manager.getPendingGeneration();

    // A dismissed Source leaves the list, and an unchanged scan does not bring it back.
    manager.dismissImports({first});
    verify(manager.getPendingImports() == std::vector<ren::AssetID>({second}), "Dismiss did not remove the Source");
    verify(manager.getPendingGeneration() > generation, "Dismiss did not change the generation");
    generation = manager.getPendingGeneration();
    manager.scheduleScan();
    manager.tick();
    tickUntilIdle(manager);
    verify(manager.getPendingImports().size() == 1 && manager.getPendingGeneration() == generation, "Unchanged scan changed the list");

    // A change to the dismissed file brings it back.
    writeText(temporary.root / "first.png", "edited");
    manager.scheduleScan();
    manager.tick();
    tickUntilIdle(manager);
    verify(manager.getPendingImports() == std::vector<ren::AssetID>({first, second}), "Changed Source did not come back");

    // A scheduled import runs on the next tick. These files are not valid images, so both imports
    // fail: they leave the pending list and stay failed until they change.
    manager.scheduleImport({first, second});
    manager.tick();
    verify(manager.getActivity() == ren::AssetActivity::Importing, "Tick did not start the import task");
    tickUntilIdle(manager);
    verify(manager.getPendingImports().empty() && manager.getFailedImports().size() == 2, "Failed imports were not recorded");
    manager.scheduleScan();
    manager.tick();
    tickUntilIdle(manager);
    verify(manager.getPendingImports().empty() && manager.getFailedImports().size() == 2, "Unchanged scan retried failed imports");

    // With auto-import on, a changed Source is imported on the tick after the scan.
    writeText(temporary.root / "second.png", "edited");
    manager.setAutoImport(true);
    manager.scheduleScan();
    manager.tick();
    tickUntilIdle(manager);
    verify(manager.getPendingImports() == std::vector<ren::AssetID>({second}), "Changed failed Source did not come back");
    manager.tick();
    verify(manager.getActivity() == ren::AssetActivity::Importing, "Auto-import did not start");
    tickUntilIdle(manager);
    verify(manager.getPendingImports().empty() && manager.getFailedImports().size() == 2, "Auto-import did not run");
  }

}  // namespace


int runLibraryTests() {
  TemporaryAssets temporary;
  const auto &root = temporary.root;
  ren::FilesystemAssetSource source(root.string());
  ren::AssetLibrary library(source);

  auto sceneID = ren::AssetID::generate();
  writeText(root / "level.scene", json({{"version", 1}, {"id", sceneID.to_string()}}).dump());
  writeText(root / "pixel.png", "not decoded by the scan");
  writeText(root / "notes.txt", "ignored");
  writeText(root / ".derived/ignored.png", "generated");


  // The first scan writes an identity sidecar, finds the scene, and ignores other files.
  library.scan();
  verify(std::filesystem::exists(root / "pixel.png.import"), "Scan did not write an identity sidecar");
  verify(!std::filesystem::exists(root / ".derived/ignored.png.import"), "Scan wrote a sidecar in the derived folder");
  auto sidecar = readJSON(root / "pixel.png.import");
  verify(sidecar.size() == 2 && sidecar.at("version") == ren::SIDECAR_VERSION, "Identity sidecar has unexpected keys");
  auto pixelID = ren::parseAssetID(sidecar.at("id"));

  auto pixel = library.find(pixelID);
  verify(pixel && pixel->kind == ren::AssetKind::Source && pixel->path == "pixel.png" && pixel->importState == ren::ImportState::NotImported,
         "Source entry is wrong");
  auto scene = library.find(sceneID);
  verify(scene && scene->kind == ren::AssetKind::Native && scene->name == "level", "Native entry is wrong");
  verify(library.findByPath("notes.txt") == nullptr && countEntries(library) == 2, "Scan added an unsupported file");


  // An unchanged scan keeps the same entry objects.
  library.scan();
  verify(library.find(pixelID) == pixel && library.find(sceneID) == scene, "Unchanged scan replaced entries");


  // A record adds the Derived entries and marks the Source as imported.
  auto kept = ren::AssetID::generate();
  auto dropped = ren::AssetID::generate();
  writeRecord(source, root, "pixel.png", pixelID, {kept, dropped});
  library.scan();
  verify(library.find(pixelID)->importState == ren::ImportState::Imported, "Record did not mark the Source as imported");
  auto keptEntry = library.find(kept);
  verify(keptEntry && keptEntry->kind == ren::AssetKind::Derived && keptEntry->source == pixelID && keptEntry->type == ren::AssetType::Texture &&
             keptEntry->path == ren::derivedPath(ren::derivedBundlePath(kept)),
         "Derived entry is wrong");
  verify(library.find(dropped) != nullptr && countEntries(library) == 4, "Record outputs are missing");


  // A new record updates the outputs by ID.
  writeRecord(source, root, "pixel.png", pixelID, {kept});
  library.scan();
  verify(library.find(kept) != nullptr && library.find(dropped) == nullptr, "Reimport did not update the outputs by ID");


  // rescan() updates one source and removes the outputs that its new record no longer lists.
  auto extra = ren::AssetID::generate();
  writeRecord(source, root, "pixel.png", pixelID, {kept, extra});
  library.rescan("pixel.png");
  verify(library.find(extra) != nullptr, "Rescan did not add an output");
  writeRecord(source, root, "pixel.png", pixelID, {kept});
  library.rescan("pixel.png");
  verify(library.find(kept) != nullptr && library.find(extra) == nullptr, "Rescan did not remove a dropped output");


  // A changed input or sidecar makes the Source stale, and keeps its outputs. A record with the
  // current stats makes it imported again. A record without stats is stale.
  auto importState = [&]() { return library.find(pixelID)->importState; };
  verify(importState() == ren::ImportState::Imported, "Record with current stats is not imported");
  writeText(root / "pixel.png", "edited");
  library.scan();
  verify(importState() == ren::ImportState::Stale && library.find(kept) != nullptr, "Edited input did not make the Source stale");
  writeRecord(source, root, "pixel.png", pixelID, {kept});
  library.scan();
  verify(importState() == ren::ImportState::Imported, "Reimport did not clear the stale state");
  writeText(root / "pixel.png.import", readJSON(root / "pixel.png.import").dump());
  library.scan();
  verify(importState() == ren::ImportState::Stale, "Edited sidecar did not make the Source stale");
  writeRecord(source, root, "pixel.png", pixelID, {kept}, false);
  library.scan();
  verify(importState() == ren::ImportState::Stale, "Record without stats is not stale");


  // A copy with the same sidecar is a conflict. The first path keeps the ID.
  std::filesystem::copy_file(root / "pixel.png", root / "copy.png");
  std::filesystem::copy_file(root / "pixel.png.import", root / "copy.png.import");
  library.scan();
  verify(library.find(pixelID)->path == "pixel.png" && library.findByPath("copy.png") == nullptr, "Copied sidecar took the ID");
  std::filesystem::remove(root / "copy.png");
  std::filesystem::remove(root / "copy.png.import");


  // A moved file and sidecar keep the ID and the outputs.
  std::filesystem::create_directories(root / "moved");
  std::filesystem::rename(root / "pixel.png", root / "moved/pixel.png");
  std::filesystem::rename(root / "pixel.png.import", root / "moved/pixel.png.import");
  library.scan();
  verify(library.find(pixelID)->path == "moved/pixel.png" && library.findByPath("pixel.png") == nullptr, "Move did not keep the ID");
  verify(library.find(kept) != nullptr, "Move removed the outputs");


  // Deleted files remove their entries, and a removed Source removes its outputs. rescan() does
  // this for one file, scan() for all.
  std::filesystem::remove(root / "level.scene");
  library.rescan("level.scene");
  verify(library.find(sceneID) == nullptr, "Rescan kept a deleted file");
  std::filesystem::remove(root / "moved/pixel.png");
  library.rescan("moved/pixel.png");
  verify(countEntries(library) == 0, "Rescan kept a deleted Source or its outputs");
  library.scan();
  verify(countEntries(library) == 0, "Scan kept entries of deleted files");

  verifyManagerTasks();
  ren::println("Asset library tests passed");
  return 0;
}
