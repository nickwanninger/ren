#pragma once

#include <flecs/flecs.h>

namespace ren {

  class Scheme;

  // Register a handful of Flecs ECS helpers with the provided Scheme VM.
  void registerEcsBindings(Scheme &vm, flecs::world &world);

}  // namespace ren

