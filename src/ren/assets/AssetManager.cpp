#include <ren/assets/AssetManager.h>

#include <ren/renderer/Texture.h>
#include <ren/renderer/shader/ShaderModule.h>
#include <ren/core/Application.h>
#include <imgui/imgui.h>
#include <ren/misc/hash.h>
#include <ren/core/UUID.h>


namespace ren {

  AssetManager &getAssetManager() { return ren::Application::get().getAssetManager(); }

  AssetManager::AssetManager()
      : store(source) {}

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



  static AssetType classifyAssetType(const std::filesystem::path &path) {
    auto extension = path.extension();
#define MAP(ext, type)  \
  if (extension == ext) \
    return type;

    MAP(".png", AssetType::Texture);
    MAP(".jpg", AssetType::Texture);
    MAP(".jpeg", AssetType::Texture);

    MAP(".glb", AssetType::Mesh);
    MAP(".gltf", AssetType::Mesh);
    MAP(".obj", AssetType::Mesh);
    MAP(".fbx", AssetType::Mesh);

#undef MAP

    return AssetType::Unknown;
  }

  Option<AssetID> AssetManager::import(const std::filesystem::path &path) {
    REN_PROFILE_FUNCTION();


    auto type = classifyAssetType(path);

    if (type == AssetType::Unknown) {
      ren::errln("Cannot import asset {}: unknown type", path.string());
      return None;
    }


    return None;
  }

  void AssetManager::scanForImports(void) {
    REN_PROFILE_FUNCTION();
    // The way an import scan works is we go over every asset in the source.
    // If it is an asset which we need to import, we check if it has a sidecar file next to it.
    // If it does, we check that the sidecar file is up to date.

    source.enumerate([&](const std::string_view &name) {
      // For now, skip sidecar files. We will handle them later.
      if (name.ends_with(".import")) {
        return;
      }


      AssetType type = classifyAssetType(name);
      // We won't handle this.
      if (type == AssetType::Unknown) {
        return;
      }

      AssetStat st;
      if (!source.stat(name, st)) {
        ren::errln("Failed to stat asset {}", name);
        return;
      }



      // if the sidecar file
      std::string sidecarPath = std::string(name) + ".import";

      AssetStat sidecarSt;
      if (!source.stat(sidecarPath, sidecarSt)) {
        // we need to import this asset, create its sidecar file to give it a UUID.
        auto id = AssetID::generate();
        nlohmann::ordered_json meta;

        meta["version"] = 1;
        meta["id"] = id.to_string();


        auto f = source.open(sidecarPath, AssetOpenFlags::Write | AssetOpenFlags::Create | AssetOpenFlags::Truncate);
        auto dumped = meta.dump(2);
        if (!f || !f->write(ByteSpan(dumped.data(), dumped.size())) || !f->flush()) {
          ren::errln("Failed to write sidecar file for asset {}", name);
          return;
        }

        ren::println("{} <- {}", id.to_string(), name);
      }

      auto sidecarFile = source.open(sidecarPath, AssetOpenFlags::Read);
      if (!sidecarFile) {
        ren::errln("Failed to open sidecar file for asset {}", name);
        return;
      }

      auto sidecarData = sidecarFile->readAllText().unwrap();
    });
  }


}  // namespace ren
