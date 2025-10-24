#pragma once

#include <flecs/flecs.h>
#include <fmt/core.h>
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
  };

  std::vector<ComponentRegistrationInfo> &getRegisteredComponents();



  namespace internal {
    bool doRegisterComponent(const ComponentRegistrationInfo &info);
  };

  template <typename T>
  static inline bool doRegisterComponent(ComponentRegistrationInfo info = {}) {
    info.typeName = flecs::_::type_name<T>();  // magical function i have access to that
                                               // turns a T into a fully qualified string.
    info.typeSize = sizeof(T);
    info.typeAlignment = alignof(T);

    fmt::println("Registering component: {} (size: {}, alignment: {})", info.typeName,
                 info.typeSize, info.typeAlignment);
    if (info.luaName) { fmt::println("  Lua Name: {}", info.luaName); }


    internal::doRegisterComponent(info);

    return true;
  }

}  // namespace ren

#define ren_register_component(Type, ...)                                                   \
  namespace __compreg {                                                                     \
    struct __COMPONENT_REGISTRAR_##__LINE__ {                                               \
      __COMPONENT_REGISTRAR_##__LINE__() { ren::doRegisterComponent<Type>({__VA_ARGS__}); } \
    };                                                                                      \
    inline __COMPONENT_REGISTRAR_##__LINE__ registrar_instance_##__LINE__;                  \
  }

#define ren_component(name, ...) \
  struct name __VA_ARGS__;       \
  ren_register_component(name)