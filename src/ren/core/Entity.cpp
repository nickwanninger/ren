#include <ren/core/Entity.h>
#include <ren/misc/json_serialize.h>

namespace ren {




#define SERIALIZE_TYPE(type)                                                           \
  {                                                                                    \
    if (auto *comp = tryGet<comp::type>()) { j["components"][#type] = (json)(*comp); } \
  }

  json Entity::serialize(void) {
    json j = {};
    j["uuid"] = fmt::format("{}", (u64)get<comp::ID>().uuid);
    j["name"] = get<comp::Name>();

    j["components"] = json::object();

#define COMP(c) \
  if (auto *comp = tryGet<c>()) { j["components"][#c] = (json)(*comp); }
#include <ren/core/Components.inc>

    return j;
  }




  void Entity::addChild(Entity child) {
    auto otherParent = child.getParent();
    if (otherParent) { otherParent.removeChild(child); }


    // add at the head of the children linked list.
    auto &rel = get<comp::Relationship>();


    rel.children.push_back(child.getUUID());
    child.setParent(*this);  // Set the parent relationship of the child.
  }

  void Entity::removeChild(Entity child) {
    if (child.getParent() != *this) {
      fmt::print("Entity {} is not a child of entity {}\n", child.getName(), getName());
      abort();
    }

    auto &rel = get<comp::Relationship>();

    auto uuidToRemove = child.getUUID();
    auto it = std::remove(rel.children.begin(), rel.children.end(), uuidToRemove);
    if (it != rel.children.end()) {
      rel.children.erase(it, rel.children.end());
    } else {
      fmt::print("Entity {} is not a child of entity {}\n", child.getName(), getName());
    }

    child.get<comp::Relationship>().parent = UUID::null;
  }



  json Entity::serializeRelationships(void) {
    json j = {};
    j["uuid"] = (u64)get<comp::ID>().uuid;
    j["name"] = get<comp::Name>().name;

    json children = json::array();
    eachChild([&children](Entity child) { children.push_back(child.serializeRelationships()); });

    j["children"] = children;

    return j;
  }

  void Entity::setParent(Entity parent) {
    if (parent) {
      get<comp::Relationship>().parent = parent.getUUID();
    } else {
      get<comp::Relationship>().parent = UUID::null;
    }
  }


}  // namespace ren