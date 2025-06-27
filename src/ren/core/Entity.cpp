#include <ren/core/Entity.h>
#include <ren/misc/json_serialize.h>

namespace ren {




#define SERIALIZE_TYPE(type)                                                          \
  {                                                                                   \
    if (auto comp = tryGet<comp::type>()) { j["components"][#type] = (json) * comp; } \
  }

  json Entity::serialize(void) {
    json j = {};
    j["uuid"] = fmt::format("{}", (u64)get<comp::ID>().uuid);
    j["name"] = get<comp::Name>();

    // SERIALIZE_TYPE(ID);
    // SERIALIZE_TYPE(Name);
    SERIALIZE_TYPE(Transform);

    return j;
  }




  void Entity::addChild(Entity child) {
    // First, remove the child from it's current position in the scene graph.
    if (auto otherParent = child.getParent()) otherParent.removeChild(child);

    // add at the head of the children linked list.
    auto &rel = get<comp::Relationship>();
    rel.children++;

    child.setParent(*this);  // Set the parent relationship of the child.
    auto front = getFirstChild();
    if (front) {
      // If there is already a first child, we need to update the linked list.
      child.setNextSibling(front);
      front.setPrevSibling(child);
      setFirstChild(child);  // Set the new first child to be the new child.
    } else {
      setFirstChild(child);  // If there are no children, set the first child to be the new child.
    }
  }

  void Entity::removeChild(Entity child) {
    if (child.getParent() != *this) {
      fmt::print("Entity {} is not a child of entity {}\n", child.getName(), getName());
      abort();
    }

    auto &rel = get<comp::Relationship>();
    rel.children--;


    // If the child is the first child, we need to update the firstChild pointer to be the next
    // sibling.
    if (rel.firstChild == child) { setFirstChild(child.getNextSibling()); }

    // now, update the linked list of the child's siblings.
    auto next = child.getNextSibling();
    auto prev = child.getPrevSibling();
    if (prev) { prev.setNextSibling(next); }
    if (next) { next.setPrevSibling(prev); }

    auto &crel = child.get<comp::Relationship>();
    crel.parent = entt::null;
    crel.nextSibling = entt::null;
    crel.prevSibling = entt::null;

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


}  // namespace ren