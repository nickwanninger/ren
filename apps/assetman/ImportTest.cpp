#include "ImportTest.h"
#include <ren/assets/AssetManager.h>
#include <ren/assets/DerivedAsset.h>
#include <ren/assets/MeshAsset.h>
#include <ren/assets/TextureAsset.h>
#include <ren/assets/importers/Importer.h>
#include <ren/core/Bundle.h>
#include <fstream>
#include <set>


namespace {

  void verify(bool condition, const std::string &message) {
    if (!condition) {
      throw std::runtime_error(message);
    }
  }


  template <typename Fn>
  void rejects(Fn &&fn, const std::string &message) {
    bool rejected = false;
    try {
      fn();
    } catch (const std::exception &) {
      rejected = true;
    }
    verify(rejected, message);
  }


  void writeBytes(const std::filesystem::path &path, const std::vector<u8> &bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    output.close();
    verify(bool(output), "Cannot write fixture: " + path.string());
  }


  std::string readText(const std::filesystem::path &path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  }


  void appendU32(std::vector<u8> &bytes, u32 value) {
    for (int shift = 0; shift < 32; shift += 8) {
      bytes.push_back((value >> shift) & 255);
    }
  }


  void makeFixture(const std::filesystem::path &root) {
    // One image serves both color and data slots, so import must preserve both formats.
    json gltf = json::parse(R"({
      "asset":{"version":"2.0"},
      "scenes":[{"nodes":[0]}], "scene":0,
      "nodes":[{"mesh":0}],
      "meshes":[{"name":"Triangle","primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1},"indices":2,"material":0}]}],
      "accessors":[
        {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[1,1,0],"max":[2,2,0]},
        {"bufferView":1,"componentType":5126,"count":3,"type":"VEC2"},
        {"bufferView":2,"componentType":5125,"count":3,"type":"SCALAR"}
      ],
      "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":24},
                     {"buffer":0,"byteOffset":60,"byteLength":12}],
      "images":[{"uri":"pixel.png"}], "textures":[{"source":0}],
      "materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}},"normalTexture":{"index":0}}]
    })");
    const float positions[] = {1, 1, 0, 2, 1, 0, 1, 2, 0};
    const float uv[] = {0, 0, 1, 0, 0, 1};
    const u32 indices[] = {0, 1, 2};
    std::vector<u8> geometry(72);
    std::memcpy(geometry.data(), positions, sizeof(positions));
    std::memcpy(geometry.data() + 36, uv, sizeof(uv));
    std::memcpy(geometry.data() + 60, indices, sizeof(indices));
    std::vector<u8> png = {137, 80, 78, 71, 13, 10,  26,  10,  0,   0,   0, 13, 73, 72, 68, 82, 0,  0,   0,   1,  0,  0,   0,   1,
                           8,   6,  0,  0,  0,  31,  21,  196, 137, 0,   0, 0,  13, 73, 68, 65, 84, 120, 156, 99, 16, 209, 176, 249,
                           15,  0,  2,  68, 1,  120, 106, 40,  52,  133, 0, 0,  0,  0,  73, 69, 78, 68,  174, 66, 96, 130};
    writeBytes(root / "pixel.png", png);
    gltf["buffers"] = json::array({{{"byteLength", geometry.size()}, {"uri", "geometry.bin"}}});
    writeBytes(root / "geometry.bin", geometry);
    std::ofstream(root / "external.gltf") << gltf.dump();

    // The GLB contains all inputs. It exercises the embedded image path too.
    gltf["buffers"][0].erase("uri");
    gltf["bufferViews"].push_back({{"buffer", 0}, {"byteOffset", geometry.size()}, {"byteLength", png.size()}});
    gltf["images"][0] = {{"bufferView", 3}, {"mimeType", "image/png"}};
    geometry.insert(geometry.end(), png.begin(), png.end());
    gltf["buffers"][0]["byteLength"] = geometry.size();
    while (geometry.size() % 4 != 0) {
      geometry.push_back(0);
    }
    auto text = gltf.dump();
    while (text.size() % 4 != 0) {
      text.push_back(' ');
    }
    std::vector<u8> glb;
    appendU32(glb, 0x46546c67);
    appendU32(glb, 2);
    appendU32(glb, 12 + 8 + text.size() + 8 + geometry.size());
    appendU32(glb, text.size());
    appendU32(glb, 0x4e4f534a);
    glb.insert(glb.end(), text.begin(), text.end());
    appendU32(glb, geometry.size());
    appendU32(glb, 0x004e4942);
    glb.insert(glb.end(), geometry.begin(), geometry.end());
    writeBytes(root / "triangle.glb", glb);
  }


  // An importer that records its options and imports nothing.
  class OptionTestImporter : public ren::Importer {
   public:
    struct Options {
      int count = 0;
      float scale = 1.0f;
      std::string label = "default";
      bool enabled = true;

      NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Options, count, scale, label, enabled)
    };

    static std::vector<std::string> getExtensions(void) { return {".option-test", ".png"}; }

    explicit OptionTestImporter(Options options)
        : options(std::move(options)) {}

    void import(ren::ImportContext &) override {}

    Options options;
  };


  void verifyImporterOptions(void) {
    ren::registerImporter<OptionTestImporter>("option-test");
    rejects([]() { ren::registerImporter<OptionTestImporter>("option-test"); }, "Registry accepted a duplicate importer name");

    const auto *info = ren::findImporter("option-test");
    verify(info && ren::findImporterForPath("a/b.OPTION-TEST") == info, "Registry lookup by name or extension failed");
    verify(ren::findImporterForPath("pixel.png") == ren::findImporter("TextureImporter"), "A later importer replaced the default for an extension");
    verify(!ren::findImporter("missing-importer") && !ren::findImporterForPath("file.unknown"), "Registry found an absent importer");

    auto options = json::parse(R"({"count":7,"scale":2,"label":"saved"})");
    auto importer = info->create(options);
    const auto &parsed = static_cast<OptionTestImporter &>(*importer).options;
    verify(parsed.count == 7 && parsed.scale == 2.0f && parsed.label == "saved" && parsed.enabled,
           "Typed options could not read persisted JSON values");
    verify(options.size() == 4 && options.at("enabled") == true && options.at("count") == 7, "Missing defaults were not filled into the options");

    auto unknown = json::parse(R"({"cuont":7})");
    rejects([&]() { info->create(unknown); }, "Importer accepted an unknown option");
    auto mistyped = json::parse(R"({"count":"7"})");
    rejects([&]() { info->create(mistyped); }, "Importer accepted an invalid option type");
  }


  void verifyTextureImport(const std::filesystem::path &root, ren::AssetManager &manager) {
    auto path = root / "standalone.PNG";
    auto sidecarPath = root / "standalone.PNG.import";
    std::filesystem::copy_file(root / "pixel.png", path);
    auto first = manager.importFile("standalone.PNG");
    auto id = ren::parseAssetID(first.at("sourceAsset"));
    verify(first.at("importer") == "TextureImporter" && first.at("derivedAssets").size() == 1 && first.at("dependencies").size() == 1,
           "Standalone texture import has an invalid record");
    auto assets = ren::readImportedAssets(manager.getSource(), first);
    const auto &texture = *assets.at(0)->as<ren::TextureAsset>();
    verify(texture.width == 1 && texture.height == 1 && texture.pixelFormat == ren::PixelFormat::RGBA8_SRGB &&
               texture.pixels == std::vector<u8>({20, 40, 60, 255}),
           "Standalone texture pixels or default color space differ");
    verify(assets[0]->sourceAsset == id, "Standalone texture lost its source link");

    auto sidecarText = readText(sidecarPath);
    verify(json::parse(sidecarText).at("options").at("srgb") == true, "Texture default was not saved in the sidecar");
    auto second = manager.importFile("standalone.PNG");
    verify(readText(sidecarPath) == sidecarText && second.at("sourceAsset") == first.at("sourceAsset") &&
               second.at("fingerprint") == first.at("fingerprint"),
           "Standalone texture reimport changed identity, settings, or fingerprint");
    verify(second.at("derivedAssets")[0].at("id") != first.at("derivedAssets")[0].at("id"), "Texture reimport reused a derived ID");
    verify(std::filesystem::exists(root / ".derived" / first.at("derivedAssets")[0].at("path").get<std::string>()),
           "Texture reimport removed the old bundle");

    auto sidecar = json::parse(sidecarText);
    sidecar["options"]["srgb"] = false;
    std::ofstream(sidecarPath) << sidecar.dump(2);
    auto linearSettings = readText(sidecarPath);
    auto linear = manager.importFile("standalone.PNG");
    verify(linear.at("fingerprint") != first.at("fingerprint") && linear.at("sourceHash") == first.at("sourceHash"),
           "Texture settings did not affect the fingerprint");
    verify(readText(sidecarPath) == linearSettings, "Texture import rewrote user settings");
    auto linearAssets = ren::readImportedAssets(manager.getSource(), linear);
    verify(linearAssets.at(0)->as<ren::TextureAsset>()->pixelFormat == ren::PixelFormat::RGBA8_UNORM,
           "Linear texture option was ignored");

    sidecar["options"]["srgb"] = "false";
    std::ofstream(sidecarPath) << sidecar.dump();
    rejects([&]() { manager.importFile("standalone.PNG"); }, "Texture importer accepted an invalid option type");
    sidecar["importer"] = "missing-importer";
    std::ofstream(sidecarPath) << sidecar.dump();
    rejects([&]() { manager.importFile("standalone.PNG"); }, "Import dispatch ignored an explicit importer");
    std::ofstream(sidecarPath) << linearSettings;
    std::ofstream(path, std::ios::binary | std::ios::trunc) << "invalid image";
    rejects([&]() { manager.importFile("standalone.PNG"); }, "Invalid texture import succeeded");
    verify(ren::readImportRecord(manager.getSource(), id) == linear, "Failed texture import replaced the published record");
    std::filesystem::remove(path);
    std::filesystem::remove(sidecarPath);
    verify(ren::readImportedAssets(manager.getSource(), ren::readImportRecord(manager.getSource(), id)).size() == 1,
           "Standalone texture bundle required its source");

    auto legacyID = ren::AssetID::generate();
    std::filesystem::copy_file(root / "pixel.png", root / "legacy.png");
    std::ofstream(root / "legacy.png.import") << json({{"version", 1}, {"id", legacyID.to_string()}}).dump();
    auto originalLegacy = readText(root / "legacy.png.import");
    std::ofstream(root / "legacy.png", std::ios::binary | std::ios::trunc) << "invalid image";
    rejects([&]() { manager.importFile("legacy.png"); }, "Invalid legacy texture import succeeded");
    verify(readText(root / "legacy.png.import") == originalLegacy, "Failed import saved defaults in the sidecar");
    std::filesystem::copy_file(root / "pixel.png", root / "legacy.png", std::filesystem::copy_options::overwrite_existing);
    auto legacy = manager.importFile("legacy.png");
    auto upgraded = json::parse(readText(root / "legacy.png.import"));
    verify(legacy.at("sourceAsset") == legacyID.to_string() && upgraded.at("importer") == "TextureImporter" && upgraded.at("options").at("srgb") == true,
           "Texture sidecar migration lost identity or settings");

    auto blockedID = ren::AssetID::generate();
    std::filesystem::copy_file(root / "pixel.png", root / "blocked.png");
    auto blockedSettings = json({{"version", 1}, {"id", blockedID.to_string()}}).dump();
    std::ofstream(root / "blocked.png.import") << blockedSettings;
    std::filesystem::create_directory(root / ren::importRecordPath(blockedID));
    rejects([&]() { manager.importFile("blocked.png"); }, "Blocked record publication succeeded");
    verify(readText(root / "blocked.png.import") == blockedSettings, "Failed publication kept newly inserted defaults");
  }


  struct TemporaryAssets {
    std::filesystem::path root = std::filesystem::temp_directory_path() / ("ren-import-test-" + ren::AssetID::generate().to_string());

    TemporaryAssets() { std::filesystem::create_directories(root); }

    ~TemporaryAssets() {
      std::error_code error;
      std::filesystem::remove_all(root, error);
    }
  };

}  // namespace


int runImportTests() {
  {
    TemporaryAssets scanAssets;
    makeFixture(scanAssets.root);
    std::ofstream(scanAssets.root / "broken.PNG") << "invalid image";
    std::filesystem::create_directories(scanAssets.root / ".derived/nested");
    std::filesystem::copy_file(scanAssets.root / "pixel.png", scanAssets.root / ".derived/nested/ignored.png");
    ren::AssetManager scanner;
    scanner.addFilesystemSource(scanAssets.root.string());
    auto scanned = scanner.scanForImports();
    verify(scanned.at("imported").size() == 3 && scanned.at("errors").size() == 1 && scanned.at("errors")[0].at("path") == "broken.PNG",
           "Scan did not import supported sources or report failures");
    verify(!std::filesystem::exists(scanAssets.root / "broken.PNG.import"), "Failed import created a sidecar");
    for (const auto &name : scanned.at("imported")) {
      auto sidecar = json::parse(readText(scanAssets.root / (name.get<std::string>() + ".import")));
      auto id = ren::parseAssetID(sidecar.at("id"));
      auto record = ren::readImportRecord(scanner.getSource(), id);
      verify(!ren::readImportedAssets(scanner.getSource(), record).empty(), "Scan created a sidecar without derived bundles");
    }
    auto previous = readText(scanAssets.root / "pixel.png.import");
    auto repeated = scanner.scanForImports();
    verify(repeated.at("imported").size() == 3 && repeated.at("errors").size() == 1 && readText(scanAssets.root / "pixel.png.import") == previous,
           "Repeated scan imported generated files or changed settings");
    verify(!std::filesystem::exists(scanAssets.root / ".derived/nested/ignored.png.import"), "Scan imported a generated file");
  }

  TemporaryAssets temporary;
  const auto &root = temporary.root;
  makeFixture(root);
  ren::AssetManager manager;
  manager.addFilesystemSource(root.string());
  verifyImporterOptions();
  verifyTextureImport(root, manager);
  auto first = manager.importFile("triangle.glb");
  auto id = ren::parseAssetID(first.at("sourceAsset"));
  auto sidecar = readText(root / "triangle.glb.import");
  verify(json::parse(sidecar).at("options").at("flipUVs") == true, "Mesh default was not saved in the sidecar");
  auto second = manager.importFile("triangle.glb");
  verify(first.at("fingerprint") == second.at("fingerprint"), "Unchanged inputs changed the import fingerprint");
  verify(readText(root / "triangle.glb.import") == sidecar, "Reimport changed the sidecar");
  verify(first.at("sourceAsset") == second.at("sourceAsset"), "Reimport changed source identity");
  std::set<std::string> oldIDs;
  for (const auto &output : first.at("derivedAssets")) {
    oldIDs.insert(output.at("id").get<std::string>());
    verify(std::filesystem::exists(root / ".derived" / output.at("path").get<std::string>()), "Reimport removed an old bundle");
  }
  for (const auto &output : second.at("derivedAssets")) {
    verify(!oldIDs.contains(output.at("id").get<std::string>()), "Reimport reused a derived ID");
  }
  auto record = ren::readImportRecord(manager.getSource(), id);
  verify(record == second, "Published record differs from the import result");
  auto assets = ren::readImportedAssets(manager.getSource(), record);
  size_t meshes = 0;
  size_t textures = 0;
  bool srgb = false;
  bool linear = false;
  for (const auto &asset : assets) {
    if (const auto *mesh = asset->as<ren::MeshAsset>()) {
      ++meshes;
      verify(mesh->indices.size() == 3 && mesh->vertices.size() == 3 && !mesh->bvh.nodes.empty(), "Geometry or baked BVH is missing");
    } else {
      ++textures;
      const auto &texture = *asset->as<ren::TextureAsset>();
      verify(texture.width == 1 && texture.height == 1 && texture.pixels == std::vector<u8>({20, 40, 60, 255}), "Decoded pixels differ");
      srgb |= texture.pixelFormat == ren::PixelFormat::RGBA8_SRGB;
      linear |= texture.pixelFormat == ren::PixelFormat::RGBA8_UNORM;
    }
  }
  verify(meshes == 1 && textures == 2 && srgb && linear, "Import lost geometry or a texture format");

  // A failure must retain the last successful record, even with a changed source.
  std::ofstream(root / "triangle.glb", std::ios::binary | std::ios::trunc) << "invalid";
  rejects([&]() { manager.importFile("triangle.glb"); }, "Invalid source import succeeded");
  verify(ren::readImportRecord(manager.getSource(), id) == second, "Failed import replaced the published record");
  std::filesystem::remove(root / "triangle.glb");
  std::filesystem::remove(root / "triangle.glb.import");
  verify(ren::readImportedAssets(manager.getSource(), ren::readImportRecord(manager.getSource(), id)).size() == assets.size(),
         "Bundle loading required the source or sidecar");

  auto external = manager.importFile("external.gltf");
  verify(external.at("dependencies").size() == 3, "External image or geometry dependency is missing");
  std::filesystem::remove(root / "pixel.png");
  auto externalID = ren::parseAssetID(external.at("sourceAsset"));
  rejects([&]() { manager.importFile("external.gltf"); }, "Missing texture import succeeded");
  verify(ren::readImportRecord(manager.getSource(), externalID) == external, "Partial import replaced the published record");
  std::filesystem::remove(root / "geometry.bin");
  verify(ren::readImportedAssets(manager.getSource(), ren::readImportRecord(manager.getSource(), externalID)).size() == 3,
         "Bundles required external inputs");

  auto changedOwner = record;
  changedOwner["sourceAsset"] = ren::AssetID::generate().to_string();
  rejects([&]() { ren::readImportedAssets(manager.getSource(), changedOwner); }, "Mismatched bundle owner passed validation");
  auto unsafe = record;
  unsafe["derivedAssets"][0]["path"] = "../../triangle.glb";
  rejects([&]() { ren::readImportedAssets(manager.getSource(), unsafe); }, "Unsafe resource path passed validation");

  auto mesh = std::find_if(assets.begin(), assets.end(), [](const auto &asset) { return asset->getType() == ren::AssetType::Mesh; });
  auto &geometry = *(*mesh)->as<ren::MeshAsset>();
  geometry.bvh.nodes[0].left = 0;
  rejects([&]() { ren::writeDerivedAsset(manager.getSource(), "invalid.asset", **mesh); }, "Cyclic BVH passed validation");
  auto bundlePath = root / ".derived" / record.at("derivedAssets")[0].at("path").get<std::string>();
  std::filesystem::resize_file(bundlePath, 3);
  rejects([&]() { ren::readDerivedAsset(manager.getSource(), ren::derivedPath(record.at("derivedAssets")[0].at("path").get<std::string>())); },
          "Truncated bundle passed validation");
  ren::println(
      "Import tests passed: texture options, importer dispatch, fresh IDs, stable settings, publication, baked BVHs, pixels, dependencies, source-free loading, invalid bundles");
  return 0;
}
