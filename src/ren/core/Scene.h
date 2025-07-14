#pragma once


#include <entt/entt.hpp>
#include <ren/core/UUID.h>

namespace ren {

  class Entity;  // fwd declaration.

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

    auto getAll() { return registry.view<entt::entity>(); }

    std::string serialize(void);

    // Grab an entity by its UUID.
    Entity getEntity(UUID uuid);

   protected:
    friend class Entity;
    entt::registry registry;

    std::unordered_map<UUID, entt::entity> entities;
  };
}  // namespace ren