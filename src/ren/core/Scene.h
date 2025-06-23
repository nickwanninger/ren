#pragma once


#include <entt/entt.hpp>
#include <ren/core/UUID.h>

namespace ren {

  class Entity;

  // A scene is a collection of entities with components that can be rendered.
  class Scene {
   public:
    // Go through these interfaces to create entities.
    // This ensures that all entities are properly registered with a name, an id,
    // a transform, and any other necessary components.
    Entity createEntity(const std::string &name = "Entity");
    Entity createEntity(UUID uuid, const std::string &name = "Entity");
    void destroyEntity(Entity entity);


    template <typename... Component>
    auto getAllWith() {
      return registry.view<Component...>();
    }

   protected:
    friend class Entity;
    entt::registry registry;

    std::unordered_map<UUID, entt::entity> entities;
  };
}  // namespace ren