#pragma once


#include <ren/types.h>
#include <ren/misc/json_serialize.h>
#include <ren/core/UUID.h>
#include <functional>

namespace ren {

  class Bundle;

  // An AssetID is just an integer.
  // The value comes from the issuing global asset manager.
  using AssetID = UUID;

  enum class AssetType {
    Texture,
    Mesh,
    Shader,
    Material,
    Script,
    Unknown,
  };

  JSON_SERIALIZE_ENUM(AssetType, {
                                     {AssetType::Texture, "Texture"},
                                     {AssetType::Mesh, "Mesh"},
                                     {AssetType::Shader, "Shader"},
                                     {AssetType::Material, "Material"},
                                     {AssetType::Script, "Script"},
                                     {AssetType::Unknown, "Unknown"},
                                 })



  // The data of one asset in memory, without GPU objects. An importer fills an asset and writes it
  // to a bundle. A loader reads the bundle back and builds GPU objects from the asset.
  //
  // A derived class holds the content and supplies these members for registerAssetType():
  //   static constexpr AssetType TYPE
  //   static ref<Derived> read(const Bundle &bundle)    Read the content that write() stores.
  class Asset {
   public:
    // The identity of the asset. `sourceAsset` is the source file that the import made it from.
    AssetID id;
    AssetID sourceAsset;
    std::string name;

    virtual ~Asset() = default;

    virtual AssetType getType(void) const = 0;

    // Throws if the content is not valid. The bundle functions call this before a write and after a read.
    virtual void validate(void) const = 0;

    // Add the content to the bundle as metadata keys and blobs. The caller writes the identity.
    virtual void write(Bundle &bundle) const = 0;

    // This asset as type T. Returns null if the asset has a different type.
    template <typename T>
    T *as(void) {
      return getType() == T::TYPE ? static_cast<T *>(this) : nullptr;
    }

    template <typename T>
    const T *as(void) const {
      return getType() == T::TYPE ? static_cast<const T *>(this) : nullptr;
    }
  };



  //=========================
  // Asset types
  //=========================

  // One registry entry. The registry owns it for the life of the program.
  struct AssetTypeInfo {
    AssetType type;

    // Read the content of an asset from a bundle. Does not set the identity and does not validate.
    std::function<ref<Asset>(const Bundle &bundle)> read;
  };


  namespace asset_detail {

    // Add an entry to the registry. Throws if the type exists.
    void addAssetType(AssetTypeInfo info);

  }  // namespace asset_detail


  // Register an asset class, so that bundles of its type can be read. Throws if the type exists.
  template <typename T>
    requires std::derived_from<T, Asset>
  void registerAssetType(void) {
    asset_detail::addAssetType({T::TYPE, [](const Bundle &bundle) -> ref<Asset> { return T::read(bundle); }});
  }


  // Find a registered asset type. The first call registers the types that ren supplies.
  // Returns null if no class is registered for the type.
  const AssetTypeInfo *findAssetType(AssetType type);

}  // namespace ren
