#pragma once

#include <ren/assets/Asset.h>
#include <ren/core/Option.h>

namespace ren {


  // T must be derived from a RuntimeAsset, and have a `static ref<T>
  // load(DerivedAsset &asset)` method that loads the runtime asset from the
  // asset.
  template <typename T>
  concept AssetTypeConstraint = std::derived_from<T, ren::RuntimeAsset> && requires(ren::Asset &asset) {
    { T::load(static_cast<ren::Asset &>(asset)) } -> std::convertible_to<ren::ref<T>>;
  };

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

    Option<ref<T>> resolve() const {
      // TODO: get the RuntimeAsset from the AssetManager by ID.
      return None;
    }

    AssetID id() const { return m_id; }

    // A handle is 'true' if it has a non-null ID.
    // This does not mean the asset is valid, though.
    operator bool() const { return m_id != UUID::null; }


   private:
    AssetID m_id = UUID::null;
  };

}  // namespace ren