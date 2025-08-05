#pragma once


#include <ren/core/UUID.h>
#include <ren/core/Entity.h>

namespace ren {


  // A scene is a collection of entities with components that can be rendered.
  class Scene {
   public:


    // Go through these interfaces to create entities.
    // This ensures that all entities are properly registered with a name, an id,
    // a transform, and any other necessary components.
    Entity createEntity(const std::string &name = "Entity");
    Entity createEntity(UUID uuid, const std::string &name = "Entity");

    json serialize(void);

    // Grab an entity by its UUID.
    Entity getEntity(UUID uuid);

    Entity getRoot(void) { return this->root; }

    // Construct the scene using some entity as a root
    Scene(ren::Entity rootEntity);

   protected:
    friend class SceneRenderer;


    void globalizeTransforms(void);

    std::unordered_map<UUID, ren::Entity> entities;

    ren::Entity root;
  };
}  // namespace ren