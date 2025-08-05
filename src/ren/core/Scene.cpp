#include <ren/core/Scene.h>
#include <ren/core/Entity.h>
#include <ren/core/Components.h>
#include <ren/core/Instrumentation.h>

#include <ren/core/Application.h>

namespace ren {


  Entity Scene::createEntity(const std::string &name) {
    REN_PROFILE_FUNCTION();
    // Defer to the overloaded version with UUID.
    return createEntity(UUID(), name);
  }

  Entity Scene::createEntity(UUID uuid, const std::string &name) {
    REN_PROFILE_FUNCTION();
    auto entity = ren::world().entity().child_of(this->root);
    // Make sure the entity has a UUID.
    entity.emplace<comp::ID>(uuid);
    // Give the entity a name
    entity.emplace<comp::Name>(name);
    // This places it at 0,0,0 by default.
    entity.emplace<comp::Transform>();

    entities[uuid] = entity;

    return entity;
  }


  static json serializeEntity(Entity &e) {
    json j = {};
    if (!e.has<comp::ID>()) {
      // If the entity does not have an ID, we cannot serialize it.
      j["error"] = "Entity does not have a UUID.";
      return j;
    }

    j["uuid"] = fmt::format("{}", (u64)e.get<comp::ID>().uuid);
    j["name"] = e.get<comp::Name>();

    j["components"] = json::object();

#define COMP(c) \
  if (auto *comp = e.try_get<c>()) { j["components"][#c] = (json)(*comp); }
#include <ren/core/Components.inc>


    j["children"] = json::array();
    e.children([&](Entity child) { j["children"].push_back(serializeEntity(child)); });

    return j;
  }


  json Scene::serialize(void) {
    REN_PROFILE_FUNCTION();
    json j;
    j["entities"] = json::array();

    root.children([&](Entity e) { j["entities"].push_back(serializeEntity(this->root)); });

    // fmt::print("MessagePack: {} bytes\n", json::to_msgpack(j).size());
    // fmt::print("Json:        {} bytes\n", j.dump().size());
    return j;
  }




  // This whole method of globalizing transforms is a bit of a hack,
  // and it shouldn't be recursive I think.
  static void globalizeChildren(Entity parent) {
    // parent's transform is the global transform.
    auto &parentTransformMatrix = parent.get<comp::Transform>().transformMatrix;

    parent.children([&](Entity child) {
      auto &t = child.get_mut<comp::Transform>();
      t.transformMatrix = parentTransformMatrix * t.getTransform();
      globalizeChildren(child);
    });
  }


  void Scene::globalizeTransforms(void) { getRoot().children(globalizeChildren); }

  Entity Scene::getEntity(UUID uuid) {
    REN_PROFILE_FUNCTION();

    auto it = entities.find(uuid);
    if (it != entities.end()) return it->second;
    return Entity();
  }

  Scene::Scene(ren::Entity rootEntity)
      : root(rootEntity) {
    REN_PROFILE_FUNCTION();
    //
  }

}  // namespace ren