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


#include <ren/assets/AssetSource.h>

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

ECS_STRUCT(Position, {
  float x;
  float y;
});

// ECS_META_IMPL_CALL(ECS_STRUCT_, ECS_META_IMPL, Position, "{ float x; float y; }");


int main(int argc, char *argv[]) {
  auto x = EcsStructType ;
#if 0
  flecs::world world;
  ECS_META_COMPONENT(world, Position);

  {
    ecs_component_desc_t desc = {0};
    ecs_entity_desc_t edesc = {0};
    edesc.id = FLECS_IDPositionID_;
    edesc.use_low_id = true;
    edesc.name = "Position";
    edesc.symbol = "Position";
    desc.entity = ecs_entity_init(world, &edesc);
    desc.type.size = (static_cast<ecs_size_t>(sizeof(Position)));
    desc.type.alignment = static_cast<int64_t>(alignof(Position));
    FLECS_IDPositionID_ = ecs_component_init(world, &desc);
  }
  if (!(FLECS_IDPositionID_ != 0)) {
    ecs_assert_log_((2), "ecs_id(Position) != 0", "/Users/nick/dev/renderer/src/ren/main.cpp", 47,
                    "failed to create component %s", "Position");
    ecs_os_api.abort_();
  }
  (__builtin_expect(!(FLECS_IDPositionID_ != 0), 0)
       ? __assert_rtn(__func__, "main.cpp", 47, "FLECS_IDPositionID_ != 0")
       : (void)0);
  ecs_meta_from_desc(world, FLECS_IDPositionID_, FLECS__Position_kind, FLECS__Position_desc);
#endif

  try {
    ren::Application app("ren", {1024, 768});
    // app.world.set_target_fps(30);

    // loadMeshIntoScene("/Users/nick/Desktop/sponza.glb");
    // loadMeshIntoScene("assets/map.obj");
    loadMeshIntoScene("assets/test/meshes/simple_scene.glb");
    // loadMeshIntoScene("assets/test/meshes/unit_cube.glb");
    // loadMeshIntoScene("assets/test/meshes/unit_cube.glb");
    // loadMeshIntoScene("/Users/nick/dev/kajiya/assets/meshes/viziers_observation_deck/scene.gltf",
    // 0.01);

    // loadMeshIntoScene(
    //     "/Users/nick/dev/kajiya/assets/meshes/flying_world_-_battle_of_the_trash_god/scene.gltf",
    //     0.002f);
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
