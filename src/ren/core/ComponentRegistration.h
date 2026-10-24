#pragma once

#include <ren/types.h>
#include <flecs/flecs.h>
#include <fmt/core.h>
#include <imgui/imgui.h>
#include <cstddef>
#include <deque>
#include <type_traits>
#include <vector>

namespace ren {




  struct ComponentField;

  // Draws the editor widget for a field of type T. Returns true when the user
  // changed the value. Specialize this for each field type that the editor can edit.
  template <typename T>
  struct FieldInspector {
    static bool inspect(T &value, const ComponentField &field);
  };


  struct ComponentFieldType {
    std::string name;
    u32 size;
    bool (*inspect)(void *value, const ComponentField &field);
  };
  struct ComponentField {
    std::string name;
    u32 offset;
    u32 size;
    // FieldFlags flags;  // derived, hidden, range, ...
    const ComponentFieldType *type;
  };


  // The default shows the field as "unknown", so that a missing specialization is visible.
  template <typename T>
  bool FieldInspector<T>::inspect(T &value, const ComponentField &field) {
    ImGui::TextDisabled("%s: unknown", field.name.c_str());
    return false;
  }


  template <>
  struct FieldInspector<glm::vec3> {
    static bool inspect(glm::vec3 &v, const ComponentField &f) { return ImGui::DragFloat3(f.name.c_str(), glm::value_ptr(v)); }
  };

  template <>
  struct FieldInspector<glm::vec2> {
    static bool inspect(glm::vec2 &v, const ComponentField &f) { return ImGui::DragFloat2(f.name.c_str(), glm::value_ptr(v)); }
  };



  template <typename T>
  const ComponentFieldType &getComponentFieldType() {
    static const ComponentFieldType type = {
        .name = flecs::_::type_name<T>(),  // magical function i have access to that
        .size = sizeof(T),
        .inspect = [](void *v, const ComponentField &f) { return FieldInspector<T>::inspect(*static_cast<T *>(v), f); },
    };
    return type;
  }


  // When registering a component, you can provide additional optional information here.
  struct ComponentRegistrationInfo {
    // If provided, this name will be used to register the component in Lua bindings (perhaps...)
    const char *luaName = nullptr;

    // The stable name that scene documents use for this component. `describe()` sets it.
    const char *name = nullptr;


    // Don't mess with these fields, they get overwritten internally.
    // --- IGNORE ---
    const char *typeName = nullptr;
    uint64_t typeSize = 0;
    uint64_t typeAlignment = 0;

    std::vector<ComponentField> fields;

    // Registers the component with a world and attaches a ComponentInfoRef to
    // it. Registration sets this, because only it knows the C++ type.
    void (*bindToWorld)(flecs::world &world, const ComponentRegistrationInfo &info) = nullptr;
  };


  // Attached to each flecs component entity, so that code which has only a
  // flecs id (such as the inspector) can find the registration info.
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
    // Splits a pointer to member `F C::*` into its class and field types.
    template <typename M>
    struct MemberPointerTraits;

    template <typename C, typename F>
    struct MemberPointerTraits<F C::*> {
      using Class = C;
      using Field = F;
    };
  }  // namespace internal


  // A component's `static void describe(ComponentBuilder<T> &b)` uses this to
  // fill its ComponentRegistrationInfo. Registration calls `describe()` once,
  // before the info goes into the registry.
  template <typename C>
  class ComponentBuilder {
   public:
    ComponentBuilder(ComponentRegistrationInfo &info)
        : info(info) {}


    // Sets the stable name that scene documents use for this component.
    ComponentBuilder &name(const char *name) {
      this->info.name = name;
      return *this;
    }


    // Adds the data member `Member` as a field, for example
    // `b.field<&Transform::translation>("translation")`. The reference stays
    // valid until the next call to `field()`.
    template <auto Member>
    ComponentField &field(const char *name) {
      using Traits = internal::MemberPointerTraits<decltype(Member)>;
      static_assert(std::is_same_v<typename Traits::Class, C>, "The field is not a member of this component");

      // A pointer to member does not give an offset in a constant expression,
      // so measure it on an instance.
      auto *base = reinterpret_cast<const std::byte *>(&this->sample);
      auto *member = reinterpret_cast<const std::byte *>(&(this->sample.*Member));

      this->info.fields.push_back(ComponentField{
          .name = name,
          .offset = static_cast<u32>(member - base),
          .size = static_cast<u32>(sizeof(typename Traits::Field)),
          .type = &getComponentFieldType<typename Traits::Field>(),
      });
      return this->info.fields.back();
    }

   private:
    ComponentRegistrationInfo &info;

    // Only `field()` uses this, to measure offsets. Components must be
    // default constructible, because the ECS and scene loading also need that.
    C sample{};
  };



  namespace internal {
    bool doRegisterComponent(const ComponentRegistrationInfo &info);
  };

  template <typename T>
  static inline bool doRegisterComponent(ComponentRegistrationInfo info = {}) {
    info.typeName = flecs::_::type_name<T>();  // magical function i have access to that
                                               // turns a T into a fully qualified string.
    info.typeSize = sizeof(T);
    info.typeAlignment = alignof(T);

    // Components without `describe()` register with no fields.
    if constexpr (requires(ComponentBuilder<T> &b) { T::describe(b); }) {
      ComponentBuilder<T> builder(info);
      T::describe(builder);
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