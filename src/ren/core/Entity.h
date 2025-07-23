#pragma once

#include <entt/entt.hpp>
#include <ren/types.h>
#include <ren/core/Scene.h>
#include <ren/core/Components.h>

namespace ren {

  class Scene;

  // This class is just a wrapper around an entt::entity
  // and a scene which manages it. It's a lightweight interface
  // to add, remove, and modify components associated with the entity.
  class Entity {
   public:
    Entity() = default;
    Entity(entt::entity handle, Scene* scene)
        : handle(handle)
        , scene(scene) {}

    ~Entity() = default;


    template <typename T, typename... Args>
    T& add(Args&&... args) {
      assert(!has<T>() && "Entity already has component!");
      T& component = scene->registry.emplace<T>(handle, std::forward<Args>(args)...);
      // scene->OnComponentAdded<T>(*this, component);
      return component;
    }

    template <typename T, typename... Args>
    T& addOrReplace(Args&&... args) {
      T& component = scene->registry.emplace_or_replace<T>(handle, std::forward<Args>(args)...);
      // scene->OnComponentAdded<T>(*this, component);
      return component;
    }

    template <typename T>
    T& get() {
      return scene->registry.get<T>(handle);
    }

    template <typename... T>
    auto& getAll() {
      return scene->registry.get<T...>(handle);
    }

    template <typename Type, typename... Func>
    auto patch(Func&&... func) {
      return scene->registry.template patch<Type>(handle, std::forward<Func>(func)...);
    }

    template <typename T>
    T* tryGet() {
      return scene->registry.try_get<T>(handle);
    }

    template <typename... T>
    bool has() {
      return scene->registry.all_of<T...>(handle);
    }

    template <typename T>
    void remove() {
      scene->registry.remove<T>(handle);
    }

    operator bool() const { return handle != entt::null; }
    operator entt::entity() const { return handle; }
    operator u32() const { return (u32)handle; }

    UUID getUUID() { return get<comp::ID>().uuid; }
    operator UUID() { return getUUID(); }

    const std::string& getName() { return get<comp::Name>().name; }

    bool operator==(const Entity& other) const {
      return handle == other.handle && scene == other.scene;
    }

    bool operator!=(const Entity& other) const { return !(*this == other); }

    json serialize(void);

    glm::vec3& translation() { return get<comp::Transform>().translation; }
    glm::quat& rotation() { return get<comp::Transform>().rotation; }
    glm::vec3 rotationEuler() { return glm::eulerAngles(get<comp::Transform>().rotation); }
    glm::vec3& scale() { return get<comp::Transform>().scale; }



    // ---- Scene Graph Relationships ---- //
    Entity getParent() { return scene->getEntity(get<comp::Relationship>().parent); }

    template <typename T>
    void eachChild(T&& callback) {
      auto& registry = scene->registry;
      auto& comp = registry.get<comp::Relationship>(handle);
      for (auto child : comp.children)
        callback(scene->getEntity(child));
    }

    void addChild(Entity child);
    void removeChild(Entity child);

    json serializeRelationships(void);

   private:
    entt::entity handle = entt::null;
    Scene* scene = nullptr;


   private:
    void setParent(Entity parent);
  };
}  // namespace ren