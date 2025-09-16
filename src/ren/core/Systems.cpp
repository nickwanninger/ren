#include <ren/core/Systems.h>
#include <ren/types.h>


namespace ren {


  // add the definitions of the phases.
#define PHASE(name) flecs::entity Before##name, name, After##name;
#include "./Phases.inc"
#undef PHASE


  void initPhases(flecs::world& world) {
#define DEFINE_PHASE(name) ren::name = world.entity("ren::phase::" #name).add(flecs::Phase);

#define PHASE(name)           \
  DEFINE_PHASE(name);         \
  DEFINE_PHASE(Before##name); \
  DEFINE_PHASE(After##name); \
  ren::name.depends_on(ren::Before##name); \
  ren::After##name.depends_on(ren::name);


#define PHASE_DEPENDS_ON(name, dep) ren::Before##name.depends_on(ren::After##dep);
#define PHASE_ROOT(name) ren::Before##name.depends_on(flecs::OnUpdate);
#include "./Phases.inc"
#undef PHASE_ROOT
#undef PHASE_DEPENDS_ON
#undef PHASE


    // Build a query that lists phases in execution order (topologically sorted)
    auto q = world.query_builder<>()
                 .with(flecs::Phase)         // match all phase entities
                 .cascade(flecs::DependsOn)  // order by DependsOn depth (like the pipeline)
                 .build();

    // Print the order
    q.each([](flecs::id id) { fmt::println("Phase: {}", id.entity().path().c_str()); });

    // ren::onWooHoo("Hello").run([](flecs::iter& it) { fmt::println("WooHoo!"); });
    // ren::onUpdate("Hello").run([](flecs::iter& it) { fmt::println("Update!"); });  //
  }


}  // namespace ren