#include <ren/core/Entity.h>
#include <ren/misc/json_serialize.h>
#include <ren/core/Application.h>

namespace ren {

  Entity createEntity() {
    auto &world = ren::world();

    // auto scene = world.lookup("scene");
    auto e = world.entity().child_of(world.entity("scene"));

    e.emplace<ren::comp::ID>();
    e.emplace<ren::comp::Name>("Empty");
    e.emplace<ren::comp::Transform>();

    return e;
  }


  glm::mat4 getWorldTransform(Entity &e) {
    glm::mat4 transform = glm::mat4(1.0f);
    auto *localTransform = e.try_get<comp::Transform>();
    if (localTransform) transform = localTransform->getTransform();

    // Apply the parent's transform
    if (auto parent = e.parent()) { transform = getWorldTransform(parent) * transform; }
    return transform;
  }
}  // namespace ren
