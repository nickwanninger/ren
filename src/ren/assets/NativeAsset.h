#pragma once


#include <ren/assets/Asset.h>

#include <string>
#include <string_view>

namespace ren {

  // A file that a person authors, such as a .material or a .scene. The file holds its own
  // AssetID, so the library reads the ID from the file and needs no sidecar.
  //
  // A native file is a JSON object with the keys "version" and "id", and the content keys
  // of its type. deserializeNativeAsset() checks the id and version, and
  // serializeNativeAsset() adds them, so a derived class reads and writes only its content. The asset does not know its
  // path: the library maps the ID to the path.
  //
  // A derived class supplies these members for registerNativeType():
  //   static constexpr AssetType TYPE
  //   static constexpr u32 version                              The format version of the content.
  //   static ref<Derived> fromContent(AssetID id, const json &content)
  class NativeAsset {
   public:
    virtual ~NativeAsset() = default;

    AssetID getAssetID(void) const { return id; }
    virtual AssetType getType(void) const = 0;

    // Writes the content keys of the file. serializeNativeAsset() adds "version" and "id".
    virtual json serializeContent(void) const = 0;

   protected:
    NativeAsset() = default;
    explicit NativeAsset(AssetID id)
        : id(id) {}

    AssetID id;
  };


  // One native type: the file extension, the content version, and the factory.
  struct NativeTypeInfo {
    AssetType type;
    // The file extension with its dot, for example ".material".
    std::string extension;
    u32 version = 0;
    std::function<ref<NativeAsset>(AssetID id, const json &content)> fromContent;
  };


  // Add an entry to the registry. Throws if the type or the extension exists.
  void addNativeType(NativeTypeInfo info);

  // Register a native class for files with `extension`. Throws if the type or the extension exists.
  template <typename T>
    requires std::derived_from<T, NativeAsset>
  void registerNativeType(std::string extension) {
    ren::addNativeType({
        .type = T::TYPE,
        .extension = std::move(extension),
        .version = T::version,
        .fromContent = [](AssetID id, const json &content) -> ref<NativeAsset> { return T::fromContent(id, content); },
    });
  }


  // Find a registered native type. The first call registers the types that ren supplies.
  // Returns null if no class is registered for the type.
  const NativeTypeInfo *findNativeType(AssetType type);

  // Find the native type for the extension of `path`. Returns null if no type uses the extension.
  const NativeTypeInfo *findNativeTypeForPath(std::string_view path);


  // Checks the id and version of a native file, then makes the asset from its content. Throws if the
  // version differs from info.version, the ID is missing or invalid, or the content is invalid.
  ref<NativeAsset> deserializeNativeAsset(const NativeTypeInfo &info, const json &file);

  // deserializeNativeAsset() for a registered type T.
  template <typename T>
    requires std::derived_from<T, NativeAsset>
  ref<T> deserializeNativeAsset(const json &file) {
    const auto *info = findNativeType(T::TYPE);
    if (info == nullptr) {
      throw std::runtime_error("Native type is not registered: " + json(T::TYPE).get<std::string>());
    }
    return std::static_pointer_cast<T>(deserializeNativeAsset(*info, file));
  }

  // Writes the file of `asset`: its content and the id+version. Throws if the type is not
  // registered, or the content is not an object or contains "version" or "id".
  json serializeNativeAsset(const NativeAsset &asset);

}  // namespace ren
