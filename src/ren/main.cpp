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

int main(int argc, char *argv[]) {
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
