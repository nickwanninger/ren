#pragma once

#include <ren/assets/AssetManager.h>
#include <ren/core/Option.h>

namespace ren {


  // T is a RuntimeAsset that AssetManager::resolve() can load: a native type, or a type with a
  // `SourceAsset` and `static ref<T> fromAsset(const SourceAsset &)`.
  template <typename T>
  concept AssetTypeConstraint = std::derived_from<T, ren::RuntimeAsset> && requires { runtimeAssetType<T>(); };

  // A handle is used in runtime to refer to a runtime asset. It is a small,
  // copyable struct that simply refers to an asset by its ID.  The handle does
  // not own the asset, it simply refers to it and on resolve it will return a
  // pointer to the asset if it is valid.
  // For example, you might have
  //   ren::Handle<ren::Texture> albedo;
  // and when you need the texture you call
  //   albedo.resolve()
  // which will return an option containing a shared pointer to the texture if
  // it is valid, or None if it is not.
  template <AssetTypeConstraint T>
  class Handle {
   public:
    Handle() = default;
    Handle(AssetID id)
        : m_id(id) {}

    Handle(const Handle<T> &) = default;
    Handle(Handle<T> &&) = default;
    Handle<T> &operator=(const Handle<T> &o) {
      m_id = o.m_id;
      return *this;
    }

    // The asset from the application's AssetManager. See AssetManager::resolve().
    Option<ref<T>> resolve() const { return getAssetManager().resolve<T>(m_id); }

    AssetID id() const { return m_id; }

    // A handle is 'true' if it has a non-null ID.
    // This does not mean the asset is valid, though.
    operator bool() const { return m_id != UUID::null; }


   private:
    AssetID m_id = UUID::null;
  };

}  // namespace ren