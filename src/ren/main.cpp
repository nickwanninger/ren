#include <iostream>
#include <vector>

#include <ren/core/Instrumentation.h>
#include <ren/core/Application.h>

#include <ren/core/Components.h>
#include <flecs/flecs.h>

#include <type_traits>
#include <imgui.h>
#include <ren/core/AutoPlugin.h>

#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <ren/core/DebugLines.hpp>
#include <ren/core/Systems.h>
#include <ren/core/Time.h>


extern "C" {
}


struct Position {
  float x, y, z;
};
extern "C" {
Position the_position;

int (*test_callback)(int, int);
}



void lua_test(void) {
  fmt::println("Position at start: {}, {}, {}", the_position.x, the_position.y, the_position.z);
  lua_State *L = luaL_newstate();
  luaL_openlibs(L);
  if (luaL_dofile(L, "assets/test.lua") != LUA_OK) {
    fmt::println("Failed to load script: {}", lua_tostring(L, -1));
    lua_pop(L, 1);
  }
  fmt::println("Position after script: {}, {}, {}", the_position.x, the_position.y, the_position.z);

  for (int i = 0; i < 1000; i++) {
    auto start = ren::timestamp();
    auto trials = 1000000;
    for (int i = 0; i < trials; i++) {
      test_callback(5, 7);
    }
    auto end = ren::timestamp();

    auto elapsed = ren::elapsed_ns(start, end);
    auto average = elapsed / (float)trials;
    fmt::println("elapsed: {} ns. avg: {} ns", elapsed, average);
  }

  lua_close(L);
}

static void debug_line_test_plugin(ren::Application &app) {
  using namespace ren;
  ren::system::onUpdate<ren::comp::Transform>("Gizmo").each(
      [](flecs::entity e, ren::comp::Transform &t) {});
}
REN_PLUGIN("Debug Line Test", debug_line_test_plugin);




void loadMeshIntoScene(const char *path, float scaleChange = 0.0f) {
  fmt::println("Loading {}...", path);
  auto mesh = ren::MeshScene::load(path);
  if (!mesh) {
    fmt::print("Failed to load mesh from {}\n", path);
    return;
  }

  auto entity = mesh->instantiate({});
  if (scaleChange != 0.0f) {
    entity.get_mut<ren::comp::Transform>().scale = glm::vec3(scaleChange);
  }
}


template <typename T>
void dump_as(void *ptr, size_t length) {
  T *typed_ptr = static_cast<T *>(ptr);
  for (size_t i = 0; i < length; i++) {
    fmt::print("[{}] = {}\n", i, typed_ptr[i]);
  }
}



int main(int argc, char *argv[]) {
  // lua_test();
  // return 0;
  try {
    ren::Application app("ren", {1920, 1080});

    // app.world.set_target_fps(30);

    // loadMeshIntoScene("/Users/nick/Desktop/sponza.glb");
    // loadMeshIntoScene("assets/test/meshes/simple_scene.glb");
    // loadMeshIntoScene("assets/test/meshes/unit_cube.glb");
    // loadMeshIntoScene("assets/test/meshes/unit_cube.glb");
    // loadMeshIntoScene("/Users/nick/dev/kajiya/assets/meshes/viziers_observation_deck/scene.gltf",
    // 0.01);
    loadMeshIntoScene(
        "/Users/nick/dev/kajiya/assets/meshes/flying_world_-_battle_of_the_trash_god/scene.gltf",
        0.002f);
    // loadMeshIntoScene("/Users/nick/Desktop/sponza.glb");
    // loadMeshIntoScene("/Users/nick/Desktop/enrico.glb");
    // loadMeshIntoScene("/Users/nick/Downloads/main_sponza/NewSponza_Main_glTF_003.gltf");
    // loadMeshIntoScene("/Users/nick/Downloads/pkg_a_curtains/NewSponza_Curtains_glTF.gltf");
    // loadMeshIntoScene("/Users/nick/Downloads/pkg_b_ivy/NewSponza_IvyGrowth_glTF.gltf");
    // loadMeshIntoScene("/Users/nick/Downloads/pkg_c_trees/NewSponza_CypressTree_glTF.gltf");

    // loadMeshIntoScene("/Users/nick/Downloads/huge_icelandic_lava_cliff_sieoz_high.glb");
    // loadMeshIntoScene("/Users/nick/Downloads/Rock.glb");
    // loadMeshIntoScene("/Users/nick/Downloads/Walk in the Woods.glb");
    // loadMeshIntoScene("/Users/nick/Downloads/Fantasy Inn.glb");
    // loadMeshIntoScene("/Users/nick/Downloads/broken_wall_slunl_high.glb");
    // loadMeshIntoScene("/Users/nick/Downloads/broken_stump_rkswd_raw.glb");

    app.run();
  } catch (const std::exception &e) { fmt::print("Error: {}\n", e.what()); }

  return 0;
}
