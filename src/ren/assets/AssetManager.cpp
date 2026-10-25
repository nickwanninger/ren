#include <ren/assets/AssetManager.h>

#include <ren/renderer/Texture.h>
#include <ren/renderer/shader/ShaderModule.h>
#include <ren/core/Application.h>
#include <imgui/imgui.h>
#include <ren/misc/hash.h>

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


  // ref<Asset> AssetManager::load(const std::string_view &path, AssetType type) {
  //   REN_PROFILE_SCOPE("Get Asset");
  //   // Check if the asset is already loaded
  //   for (const auto &pair : assetRegistry) {
  //     if (pair.second.path == path) {
  //       fmt::print("Asset {} is already imported with ID {}\n", path, (u64)pair.first);
  //       return getAsset(pair.second.id);
  //     }
  //   }
  bool loadAssetBytes(const std::string_view &path, std::vector<u8> &out) { return getAssetManager().load(path, out); }


  //   if (type == AssetType::Unknown) {
  //     auto extension = [&](const char *ext, AssetType t) {
  //       // I don't have string_view::ends_with, so we do it manually.
  //       if (path.size() >= strlen(ext) && path.compare(path.size() - strlen(ext), strlen(ext), ext) == 0) {
  //         type = t;
  //         fmt::print("Detected asset type {} for path {}\n", static_cast<int>(t), path);
  //       }
  //     };

  //     // Texture formats we support
  //     extension(".png", AssetType::Texture);
  //     extension(".jpg", AssetType::Texture);
  //     extension(".jpeg", AssetType::Texture);

  //     // Mesh formats we support
  //     extension(".glb", AssetType::Mesh);
  //     extension(".gltf", AssetType::Mesh);
  //     extension(".obj", AssetType::Mesh);
  //     extension(".fbx", AssetType::Mesh);

  //     // Shader formats we support
  //     extension(".spv", AssetType::Shader);
  //     extension(".vert", AssetType::Shader);
  //     extension(".frag", AssetType::Shader);
  //     extension(".comp", AssetType::Shader);
  //     extension(".geom", AssetType::Shader);


  //     // Scripts
  //     extension(".lua", AssetType::Script);
  //     extension(".fnl", AssetType::Script);
  //   }

  //   // Create a new AssetInfo
  //   AssetInfo info;
  //   info.id = UUID();  // Generate a new UUID for the asset
  //   info.path = path;
  //   info.type = type;
  //   assetRegistry[info.id] = info;  // Store the asset info in the map
  //   // sync();                         // sync the asset state!

  //   return load(info);
  // }



}  // namespace ren
