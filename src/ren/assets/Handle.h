#pragma once

#include <ren/assets/AssetManager.h>
#include <ren/core/Option.h>

namespace ren {


  // T is a RuntimeAsset that AssetManager::resolve() can load: a native type, or a type with a
  // `SourceAsset` and `static ref<T> fromAsset(const SourceAsset &)`.
  template <typename T>
  concept AssetTypeConstraint = std::derived_from<T, ren::RuntimeAsset> && requires { runtimeAssetType<T>(); };

  // A reference to a runtime asset that can be saved. A handle refers to one of two things:
  //   - An asset, by its ID. resolve() asks the application's AssetManager, which owns the
  //     object, so a reimport shows in every handle to the asset.
  //   - An object that code made, which has no asset ID. The handle keeps the object alive,
  //     because nothing can load it again.
  // An object from an asset becomes a reference to that asset, so the handle does not keep it.
  // Only a reference to an asset can be saved: see isPersistent().
  //
  // For example, a material field can hold either kind:
  //   ren::Handle<ren::Texture> albedo = textureID;                  // an asset
  //   ren::Handle<ren::Texture> noise = ren::Texture::create(...);   // an object made in code
  template <AssetTypeConstraint T>
  class Handle {
   public:
    Handle() = default;
    Handle(AssetID id)
        : m_id(id) {}

    Handle(ref<T> object) {
      if (object && object->getAssetID() != UUID::null) {
        m_id = object->getAssetID();
      } else {
        m_object = std::move(object);
      }
    }

    // The object, or None if the handle is empty or its asset does not resolve.
    // See AssetManager::resolve().
    Option<ref<T>> resolve() const {
      if (m_object) {
        return Some(m_object);
      }
      if (m_id == UUID::null) {
        return None;
      }
      return getAssetManager().resolve<T>(m_id);
    }

    // The asset ID, or null for an empty handle or an object that code made.
    AssetID id() const { return m_id; }

    // True if the handle refers to an asset, so that a file can store it.
    bool isPersistent() const { return m_id != UUID::null; }

    // True if the handle refers to an asset or an object. The asset can still fail to resolve.
    explicit operator bool() const { return isPersistent() || m_object != nullptr; }

    bool operator==(const Handle<T>&) const = default;


   private:
    AssetID m_id = UUID::null;
    ref<T> m_object;
  };

}  // namespace ren