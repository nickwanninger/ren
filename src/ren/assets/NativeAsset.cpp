#include <ren/assets/NativeAsset.h>
#include <ren/assets/Material.h>
#include <ren/core/Bundle.h>

#include <filesystem>
#include <map>
#include <mutex>

namespace ren {

  // Entries are never removed, so pointers to them stay valid.
  struct NativeTypeRegistry {
    std::mutex mutex;
    std::map<AssetType, NativeTypeInfo> types;
    std::map<std::string, AssetType> extensions;
  };

  static NativeTypeRegistry &getRegistry(void) {
    static NativeTypeRegistry registry;
    return registry;
  }


  static std::string typeName(AssetType type) { return json(type).get<std::string>(); }


  void addNativeType(NativeTypeInfo info) {
    const auto type = info.type;
    {
      auto &registry = getRegistry();
      std::scoped_lock lock(registry.mutex);

      if (registry.types.contains(type)) {
        throw std::runtime_error("Native type is already registered: " + typeName(type));
      }
      if (registry.extensions.contains(info.extension)) {
        throw std::runtime_error("Native extension is already registered: " + info.extension);
      }
      registry.extensions.emplace(info.extension, type);
      registry.types.emplace(type, std::move(info));
    }

    // A derived bundle of the type holds its content. Register the reader without the native
    // lock, because the bundle registry has its own lock.
    asset_detail::addAssetType({type, [type](const Bundle &bundle) -> ref<Asset> { return NativeContentAsset::read(type, bundle); }});
  }


  void registerBuiltinNativeTypes(void) {
    static std::once_flag once;
    std::call_once(once, []() { registerNativeType<Material>(".material"); });
  }


  const NativeTypeInfo *findNativeType(AssetType type) {
    registerBuiltinNativeTypes();

    auto &registry = getRegistry();
    std::scoped_lock lock(registry.mutex);

    auto it = registry.types.find(type);
    return it == registry.types.end() ? nullptr : &it->second;
  }


  const NativeTypeInfo *findNativeTypeForPath(std::string_view path) {
    registerBuiltinNativeTypes();

    auto &registry = getRegistry();
    std::scoped_lock lock(registry.mutex);

    auto extension = registry.extensions.find(std::filesystem::path(path).extension().string());
    if (extension == registry.extensions.end()) {
      return nullptr;
    }
    return &registry.types.at(extension->second);
  }




  ref<NativeAsset> deserializeNativeAsset(const NativeTypeInfo &info, const json &file) {
    const auto name = typeName(info.type);
    const bool hasVersion = file.is_object() && file.contains("version") && file["version"].is_number_integer();
    if (!hasVersion || file["version"] != info.version) {
      throw std::runtime_error(name + " version mismatch");
    }
    if (!file.contains("id") || !file["id"].is_string()) {
      throw std::runtime_error(name + " missing id");
    }

    const auto text = file["id"].get<std::string>();
    const auto id = UUID::parse(text);
    if (id.isNone() || id.unwrap() == UUID::null) {
      throw std::runtime_error(name + " has an invalid id: " + text);
    }

    // The content is the file without the id and version.
    auto content = file;
    content.erase("version");
    content.erase("id");
    return info.fromContent(id.unwrap(), content);
  }


  json serializeNativeAsset(const NativeAsset &asset) {
    const auto *info = findNativeType(asset.getType());
    if (info == nullptr) {
      throw std::runtime_error("Native type is not registered: " + typeName(asset.getType()));
    }

    auto file = asset.serializeContent();
    if (!file.is_object() || file.contains("version") || file.contains("id")) {
      throw std::runtime_error(typeName(asset.getType()) + " content must be an object without 'version' or 'id'");
    }
    file["version"] = info->version;
    file["id"] = asset.getAssetID().to_string();
    return file;
  }



  //=========================
  // Derived bundles
  //=========================

  void NativeContentAsset::validate(void) const {
    if (findNativeType(type) == nullptr) {
      throw std::runtime_error("Native type is not registered: " + typeName(type));
    }
    if (!content.is_object() || content.contains("version") || content.contains("id")) {
      throw std::runtime_error(typeName(type) + " content must be an object without 'version' or 'id'");
    }
  }


  void NativeContentAsset::write(Bundle &bundle) const { bundle.setKey("content", content); }


  ref<NativeContentAsset> NativeContentAsset::read(AssetType type, const Bundle &bundle) {
    return make<NativeContentAsset>(type, bundle.getMetadata().at("content"));
  }


  json NativeContentAsset::serializeNativeFile(void) const {
    const auto *info = findNativeType(type);
    if (info == nullptr) {
      throw std::runtime_error("Native type is not registered: " + typeName(type));
    }

    auto file = content;
    file["version"] = info->version;
    file["id"] = id.to_string();
    return file;
  }

}  // namespace ren
