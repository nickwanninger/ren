#include <ren/core/Scene.h>
#include <ren/core/Entity.h>
#include <ren/core/Components.h>
#include <ren/core/Instrumentation.h>


namespace ren {


  Entity Scene::createEntity(const std::string &name) {
    REN_PROFILE_FUNCTION();
    // Defer to the overloaded version with UUID.
    return createEntity(UUID(), name);
  }

  Entity Scene::createEntity(UUID uuid, const std::string &name) {
    REN_PROFILE_FUNCTION();
    Entity entity(registry.create(), this);

    // Make sure the entity has a UUID.
    entity.add<comp::ID>(uuid);
    // Give the entity a name
    entity.add<comp::Name>(name);
    // This places it at 0,0,0 by default.
    entity.add<comp::Transform>();
    // Add a default relationship component.
    entity.add<comp::Relationship>();

    // Then register the entity with the scene.
    entities[uuid] = entity;
    return entity;
  }


  void Scene::destroyEntity(Entity entity) {
    REN_PROFILE_FUNCTION();
    if (entity) {
      // Remove the entity from the scene's entity map.
      auto it = entities.find(entity.get<comp::ID>().uuid);
      if (it != entities.end()) { entities.erase(it); }
      // Destroy the entity in the registry.
      registry.destroy(entity);
    }
  }

  std::string Scene::serialize(void) {
    REN_PROFILE_FUNCTION();
    json j;

    std::vector<json> serializedEntities;
    for (auto &[uuid, entity] : this->entities) {
      json e = Entity(entity, this).serialize();
      serializedEntities.push_back(e);
    }
    j["entities"] = serializedEntities;


    // fmt::print("MessagePack: {} bytes\n", json::to_msgpack(j).size());
    // fmt::print("Json:        {} bytes\n", j.dump().size());
    return j.dump(2);
  }


  Entity Scene::getEntity(UUID uuid) {
    REN_PROFILE_FUNCTION();
    auto it = entities.find(uuid);
    if (it != entities.end()) return Entity(it->second, this);
    return Entity();
  }
}  // namespace ren