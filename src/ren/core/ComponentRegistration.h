#pragma once

#include <ren/types.h>
#include <ren/obj/Types.h>
#include <flecs/flecs.h>
#include <fmt/core.h>
#include <cstddef>
#include <deque>
#include <type_traits>
#include <vector>

namespace ren {




  // When registering a component, you can provide additional optional information here.
  struct ComponentRegistrationInfo {
    // If provided, this name will be used to register the component in Lua bindings (perhaps...)
    const char *luaName = nullptr;

    // Don't mess with these fields, they get overwritten internally.
    // --- IGNORE ---
    const char *typeName = nullptr;
    uint64_t typeSize = 0;
    uint64_t typeAlignment = 0;

    // The authoring schema. Its name is the stable name that scene documents use.
    // Null when the component has no `describe()`.
    const obj::Type *type = nullptr;

    // Registers the component with a world and attaches a ComponentInfoRef to
    // it. Registration sets this, because only it knows the C++ type.
    void (*bindToWorld)(flecs::world &world, const ComponentRegistrationInfo &info) = nullptr;
  };


  // Attached to each flecs component entity, so that code which has only a
  // flecs id (such as an editor) can find the registration info.
  struct ComponentInfoRef {
    const ComponentRegistrationInfo *info = nullptr;
  };


  // A deque, so that ComponentInfoRef pointers stay valid when more
  // components register later (for example, from Lua).
  std::deque<ComponentRegistrationInfo> &getRegisteredComponents();

  // Calls `bindToWorld` for each registered component. Call it once, after the
  // world exists and before anything reads ComponentInfoRef.
  void bindRegisteredComponents(flecs::world &world);



  namespace internal {
    bool doRegisterComponent(const ComponentRegistrationInfo &info);
  };

  template <typename T>
  static inline bool doRegisterComponent(ComponentRegistrationInfo info = {}) {
    info.typeName = flecs::_::type_name<T>();  // magical function i have access to that
                                               // turns a T into a fully qualified string.
    info.typeSize = sizeof(T);
    info.typeAlignment = alignof(T);

    // Components without `describe()` register with no schema.
    if constexpr (requires(obj::TypeBuilder<T> &b) { T::describe(b); }) {
      info.type = &obj::getType<T>();
    }

    info.bindToWorld = [](flecs::world &world, const ComponentRegistrationInfo &info) {
      world.component<T>().template set<ComponentInfoRef>({&info});
    };

    internal::doRegisterComponent(info);

    return true;
  }



}  // namespace ren


#define REN_COMPREG_CONCAT_IMPL(a, b) a##b
#define REN_COMPREG_CONCAT(a, b) REN_COMPREG_CONCAT_IMPL(a, b)
#define REN_COMPREG_UNIQUE_NAME(base) REN_COMPREG_CONCAT(base, __LINE__)

#define ren_register_component(Type, ...)                                                   \
  namespace __compreg {                                                                     \
    struct REN_COMPREG_UNIQUE_NAME(COMPREG) {                                               \
      REN_COMPREG_UNIQUE_NAME(COMPREG)() { ren::doRegisterComponent<Type>({__VA_ARGS__}); } \
    };                                                                                      \
    inline REN_COMPREG_UNIQUE_NAME(COMPREG) REN_COMPREG_UNIQUE_NAME(COMPREG_INSTANCE);      \
  }

#define ren_component(name, ...) \
  struct name __VA_ARGS__;       \
  ren_register_component(name)