#pragma once

#include <ren/types.h>
#include <ren/core/Components.h>

#include <flecs.h>

namespace ren {

  using Entity = flecs::entity;

  inline UUID getUUID(Entity &e) { return e.get<comp::ID>().uuid; }

}  // namespace ren