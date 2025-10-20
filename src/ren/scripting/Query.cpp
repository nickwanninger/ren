#include <ren/core/AutoPlugin.h>
#include <ren/core/Application.h>
#include <flecs/flecs.h>

// Binding functions to generate ECS queries in luajit. These are called from
// lua, but we don't register them directly. use `require `ecs.query.*` to
// access.


extern "C" ecs_query_t *__ren_ecs_query_from_expr(const char *expr) {
  auto &world = ren::world();

  ecs_query_desc_t desc = {0};
  desc.expr = expr;

  auto *q = ecs_query_init(world, &desc);

  return q;
}

extern "C" uint32_t __ren_ecs_query_iter_count(ecs_iter_t *it) {
  // TODO: this should really just be loaded...
  return it->count;
}