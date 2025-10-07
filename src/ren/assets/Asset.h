#pragma once


#include <ren/types.h>
#include <ren/misc/json_serialize.h>

namespace ren {


  // An AssetID is just an integer.
  // The value comes from the issuing global asset manager.
  using AssetID = u64;

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



  // An asset is a base class for all assets in the engine.
  // Things like textures, meshes, shaders, etc are considered assets.
  // Assets are identified by their UUID.
  class Asset {
   public:
    Asset() = default;
    virtual ~Asset() = default;
    virtual AssetType getType() = 0;

    // No copy or move semantics
    Asset(const Asset&) = delete;
    Asset& operator=(const Asset&) = delete;
    Asset(Asset&&) = delete;
    Asset& operator=(Asset&&) = delete;

    AssetID getAssetID() const { return this->assetID; }

   protected:
    friend class AssetManager;
    // Set the asset ID for this asset. This is used by the AssetManager to
    // register the asset in the asset registry.
    void setAssetID(AssetID id) { this->assetID = id; }

   private:
    AssetID assetID;
  };


  namespace impl {
    template <AssetType StaticType>
    class TypedAsset : public Asset {
     public:
      virtual ~TypedAsset() = default;
      static inline AssetType getStaticType() { return StaticType; }
      inline AssetType getType() override { return StaticType; }
    };
  }  // namespace impl


  using TextureAsset = impl::TypedAsset<AssetType::Texture>;
  using MeshAsset = impl::TypedAsset<AssetType::Mesh>;
  using MaterialAsset = impl::TypedAsset<AssetType::Material>;
  using ShaderAsset = impl::TypedAsset<AssetType::Shader>;


}  // namespace ren