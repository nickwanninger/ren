#include <ren/assets/Asset.h>
#include <ren/assets/MeshAsset.h>
#include <ren/assets/TextureAsset.h>

#include <map>
#include <mutex>

namespace ren {

  // Entries are never removed, so pointers to them stay valid.
  struct AssetTypeRegistry {
    std::mutex mutex;
    std::map<AssetType, AssetTypeInfo> types;
  };

  static AssetTypeRegistry &getRegistry(void) {
    static AssetTypeRegistry registry;
    return registry;
  }


  void asset_detail::addAssetType(AssetTypeInfo info) {
    auto &registry = getRegistry();
    std::scoped_lock lock(registry.mutex);

    auto type = info.type;
    if (!registry.types.emplace(type, std::move(info)).second) {
      throw std::runtime_error("Asset type is already registered: " + json(type).get<std::string>());
    }
  }


  const AssetTypeInfo *findAssetType(AssetType type) {
    static std::once_flag once;
    std::call_once(once, []() {
      registerAssetType<MeshAsset>();
      registerAssetType<TextureAsset>();
    });

    auto &registry = getRegistry();
    std::scoped_lock lock(registry.mutex);

    auto it = registry.types.find(type);
    return it == registry.types.end() ? nullptr : &it->second;
  }

}  // namespace ren
