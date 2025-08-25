#include <ren/core/Entity.h>
#include <ren/misc/json_serialize.h>
#include <ren/core/Application.h>

namespace ren {

  Entity createEntity() {
    auto &world = ren::world();

    auto scene = world.lookup("scene");
    auto e = world.entity().child_of(scene);

    return e;
  }

}  // namespace ren
