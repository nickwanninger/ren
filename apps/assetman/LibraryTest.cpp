#include "LibraryTest.h"
#include <ren/assets/AssetLibrary.h>
#include <ren/assets/DerivedAsset.h>
#include <ren/core/Logging.h>
#include <fstream>


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


  // Write an import record that lists one Texture output for each ID.
  void writeRecord(const std::filesystem::path &root, ren::AssetID sourceAsset, const std::vector<ren::AssetID> &outputs) {
    json derived = json::array();
    for (auto id : outputs) {
      derived.push_back({{"id", id.to_string()}, {"name", "out-" + id.to_string()}, {"path", ren::derivedBundlePath(id)}, {"type", "Texture"}});
    }
    json record = {{"version", ren::IMPORT_RECORD_VERSION}, {"sourceAsset", sourceAsset.to_string()}, {"derivedAssets", derived}};
    writeText(root / ren::importRecordPath(sourceAsset), record.dump());
  }


  size_t countEntries(const ren::AssetLibrary &library) {
    size_t count = 0;
    library.forEach([&](const ren::ref<const ren::AssetEntry> &) { count++; });
    return count;
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
  writeRecord(root, pixelID, {kept, dropped});
  library.scan();
  verify(library.find(pixelID)->importState == ren::ImportState::Imported, "Record did not mark the Source as imported");
  auto keptEntry = library.find(kept);
  verify(keptEntry && keptEntry->kind == ren::AssetKind::Derived && keptEntry->source == pixelID && keptEntry->type == ren::AssetType::Texture &&
             keptEntry->path == ren::derivedPath(ren::derivedBundlePath(kept)),
         "Derived entry is wrong");
  verify(library.find(dropped) != nullptr && countEntries(library) == 4, "Record outputs are missing");


  // A new record updates the outputs by ID.
  writeRecord(root, pixelID, {kept});
  library.scan();
  verify(library.find(kept) != nullptr && library.find(dropped) == nullptr, "Reimport did not update the outputs by ID");


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


  // Deleted files remove their entries, and a removed Source removes its outputs.
  std::filesystem::remove(root / "level.scene");
  std::filesystem::remove(root / "moved/pixel.png");
  library.scan();
  verify(countEntries(library) == 0, "Scan kept entries of deleted files");

  ren::println("Asset library tests passed");
  return 0;
}
