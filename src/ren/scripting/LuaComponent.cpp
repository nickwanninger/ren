#include <sol/sol.hpp>
#include <ren/core/AutoPlugin.h>
#include <ren/core/Application.h>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <luajit.h>
}


struct LuaComponent {
  static constexpr uint32_t expected_magic = 0xC0DE1111;

  uint32_t magic = LuaComponent::expected_magic;
  int reg_id;


  LuaComponent()
      : reg_id(-1) {}

  void set(lua_State *L) {
    // if the value $v on the top of the stack is not a table, make a new table like this: {value =
    // $v}
    if (!lua_istable(L, -1)) {
      ren::dbgln("LuaComponent::set() wrapping non-table value into table {{value = ...}}");
      lua_newtable(L);
      lua_pushvalue(L, -2);  // copy the value
      lua_setfield(L, -2, "value");
      lua_remove(L, -3);  // remove the original value
    }

    if (reg_id != -1) {
      lua_rawseti(L, LUA_REGISTRYINDEX, reg_id);
    } else {
      reg_id = luaL_ref(L, LUA_REGISTRYINDEX);
    }

    ren::dbgln("LuaComponent::set() reg_id = {}", reg_id);
  }

  int get(lua_State *L) {
    if (reg_id == -1) {
      lua_pushnil(L);
      return 1;
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, reg_id);
    return 1;
  }

  ~LuaComponent() {
    if (reg_id != -1) {
      auto *L = ren::lua();
      luaL_unref(L, LUA_REGISTRYINDEX, reg_id);
      reg_id = -1;
      ren::dbgln("LuaComponent::~LuaComponent() unrefed reg_id");
    }
  }
};

static int l_write_component(lua_State *L) {
  // 3 args.
  // 1. entity id
  // 2. component id.
  // 3. component data (lua value).

  if (lua_type(L, 1) != 10) { return 0; }
  auto entity_id = *(flecs::entity_t *)lua_topointer(L, 1);  // Extract the entity id (64-bit int)

  if (lua_type(L, 2) != 10) { return 0; }
  auto component_id =
      *(flecs::entity_t *)lua_topointer(L, 2);  // Extract the component id (64-bit int)


  flecs::entity e = ren::world().entity(entity_id);


  auto *comp_ptr = (LuaComponent *)e.ensure(component_id);
  ren::dbgln("l_write_component: entity_id = {}, component_id = {}, comp_ptr = {}", entity_id,
               component_id, (void *)comp_ptr);
  if (comp_ptr == nullptr) { return 0; }

  comp_ptr->set(L);

  return 0;
}



static int l_read_component(lua_State *L) {
  // 3 args.
  // 1. entity id
  // 2. component id.

  // I trust you to pass the right id and all that.


  if (lua_type(L, 1) != 10) { return 0; }
  auto entity_id = *(flecs::entity_t *)lua_topointer(L, 1);  // Extract the entity id (64-bit int)

  if (lua_type(L, 2) != 10) { return 0; }
  auto component_id =
      *(flecs::entity_t *)lua_topointer(L, 2);  // Extract the component id (64-bit int)

  flecs::entity e = ren::world().entity(entity_id);
  auto *comp_ptr = (LuaComponent *)e.try_get_mut(component_id);

  if (!comp_ptr) {
    lua_pushnil(L);
    return 1;
  }

  return comp_ptr->get(L);
}


static int l_has_component(lua_State *L) {
  // 3 args.
  // 1. entity id
  // 2. component id.

  if (lua_type(L, 1) != 10) { return 0; }
  auto entity_id = *(flecs::entity_t *)lua_topointer(L, 1);  // Extract the entity id (64-bit int)

  if (lua_type(L, 2) != 10) { return 0; }
  auto component_id =
      *(flecs::entity_t *)lua_topointer(L, 2);  // Extract the component id (64-bit int)

  flecs::entity e = ren::world().entity(entity_id);
  auto *comp_ptr = (LuaComponent *)e.try_get_mut(component_id);

  lua_pushboolean(L, comp_ptr != nullptr);
  return 1;
}



static int l_remove_component(lua_State *L) {
  // 3 args.
  // 1. entity id
  // 2. component id.

  if (lua_type(L, 1) != 10) { return 0; }
  auto entity_id = *(flecs::entity_t *)lua_topointer(L, 1);  // Extract the entity id (64-bit int)

  if (lua_type(L, 2) != 10) { return 0; }
  auto component_id =
      *(flecs::entity_t *)lua_topointer(L, 2);  // Extract the component id (64-bit int)

  flecs::entity e = ren::world().entity(entity_id);
  e.remove(component_id);

  return 0;
}

extern "C" ecs_entity_t __ren_lua_component_create(const char *name) {
  auto &world = ren::world();


  auto entity = world.entity(name, ".", ".");
  ecs_entity_t id = entity.id();

  ecs_component_desc_t componentDesc = {};
  componentDesc.entity = id;
  componentDesc.type.size = sizeof(LuaComponent);
  componentDesc.type.alignment = alignof(LuaComponent);

  componentDesc.type.hooks.ctor = +[](void *ptr, int32_t count, const ecs_type_info_t *type_info) {
    auto *c = static_cast<LuaComponent *>(ptr);
    for (int i = 0; i < count; i++) {
      new (&c[i]) LuaComponent();
    }
  };
  componentDesc.type.hooks.dtor = +[](void *ptr, int32_t count, const ecs_type_info_t *type_info) {
    auto *c = static_cast<LuaComponent *>(ptr);
    for (int i = 0; i < count; i++) {
      c[i].~LuaComponent();
    }
  };


  id = ecs_component_init(world, &componentDesc);
  // setup destructor calls
  ecs_assert(id != 0, ECS_INTERNAL_ERROR, NULL);



  return id;
}

// static const struct luaL_reg lua_component_lib[] = {
//     {"read", l_read_component},    // Read a component
//     {"write", l_write_component},  // Write a component
//     {NULL, NULL}                   /* sentinel */
// };


REN_PLUGIN("ren_lua_component", [](ren::Application &app) {
  auto &lua = ren::sol();

  lua["package"]["preload"]["ren_lua_component"] = [=](sol::this_state s) {
    sol::state_view L(s);
    sol::table mod = L.create_table();
    mod.set_function("read", l_read_component);
    mod.set_function("write", l_write_component);
    mod.set_function("has", l_has_component);
    mod.set_function("remove", l_remove_component);

    return mod;
  };
});
