#pragma once

#include <flecs/flecs.h>
#include <ren/core/Application.h>


namespace ren {

  // Declare all the phases as extern. Defined in Systems.cpp
#define PHASE(name) extern flecs::entity Before##name, name, After##name;
#include "Phases.inc"
#undef PHASE

  // Initialize the phases in a given world.
  // This world will be the singletone world in the application, but I like to not rely on that.
  void initPhases(flecs::world &world);



  namespace system {

    // wrap flecs::world::system to ensure systems are always created with a name some phase.
    template <typename... Comps, typename... Args>
    inline auto make_system(flecs::entity_t the_kind, const char *system_name, Args &&...args) {
      return flecs::system_builder<Comps...>(ren::world(), system_name, FLECS_FWD(args)...)
          .kind(the_kind);
    }

#define DEFINE_SYSTEM_FOR_PHASE(name, phase)                                           \
  template <typename... Comps, typename... Args>                                       \
  inline auto name(const char *system_name, Args &&...args) {                          \
    return ren::system::make_system<Comps...>(phase, system_name, FLECS_FWD(args)...); \
  }

    DEFINE_SYSTEM_FOR_PHASE(onUpdate, flecs::OnUpdate)

#define PHASE(name)                                        \
  DEFINE_SYSTEM_FOR_PHASE(on##name, ren::name)             \
  DEFINE_SYSTEM_FOR_PHASE(before##name, ren::Before##name) \
  DEFINE_SYSTEM_FOR_PHASE(after##name, ren::After##name)
#include "Phases.inc"
#undef PHASE

  }  // namespace system

}  // namespace ren