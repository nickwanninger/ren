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
  class NativeAsset : public RuntimeAsset {
   public:
    virtual AssetType getType(void) const = 0;

    // Writes the content keys of the file. serializeNativeAsset() adds "version" and "id".
    virtual json serializeContent(void) const = 0;

   protected:
    NativeAsset() = default;
    explicit NativeAsset(AssetID id)
        : RuntimeAsset(id) {}
  };


  // One native type: the file extension, the content version, and the factory.
  struct NativeTypeInfo {
    AssetType type;
    // The file extension with its dot, for example ".material".
    std::string extension;
    u32 version = 0;
    std::function<ref<NativeAsset>(AssetID id, const json &content)> fromContent;
  };


  // Add an entry to the registry, and a bundle reader for NativeContentAsset of the type.
  // Throws if the type or the extension exists.
  void addNativeType(NativeTypeInfo info);

  // Registers the native types that ren supplies, one time. findNativeType() and findAssetType()
  // call it, so that the bundle readers of native types exist before a derived bundle is read.
  void registerBuiltinNativeTypes(void);

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



  // The content of a native type as a derived bundle. An importer makes CPU data only, so it
  // writes the content JSON and does not make the runtime object. Loading it is the same as
  // loading a native file: pass serializeNativeFile() to deserializeNativeAsset().
  //
  // The bundle stores the content in the metadata key "content" and has no blobs. The bundle
  // header holds the ID. The version is the current version of the native type.
  class NativeContentAsset : public Asset {
   public:
    NativeContentAsset(AssetType type, json content)
        : type(type)
        , content(std::move(content)) {}

    // A registered native type.
    AssetType type;
    // The keys of the native file without "version" and "id".
    json content;

    AssetType getType(void) const override { return type; }

    // Throws if the type is not a registered native type, or the content is not an object
    // without "version" and "id".
    void validate(void) const override;

    void write(Bundle &bundle) const override;
    static ref<NativeContentAsset> read(AssetType type, const Bundle &bundle);

    // The JSON of the native file with this content, ID and the current version of the type.
    json serializeNativeFile(void) const;
  };

}  // namespace ren
