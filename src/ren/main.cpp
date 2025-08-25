#include <iostream>
#include <vector>

#include <ren/core/Instrumentation.h>
#include <ren/core/Application.h>

#include <ren/core/Components.h>
#include <flecs.h>

#include <type_traits>
#include <imgui.h>
#include <ren/core/AutoPlugin.h>

#include <imgui.h>

struct CubeWave {};

static void test_plugin(ren::Application &app) {
  app.world.component<CubeWave>();

  app.world.system<ren::comp::Transform>("ren.core.TestSystem")
      .with<CubeWave>()
      .multi_threaded(true)
      .each([](ren::comp::Transform &t) {
        // REN_PROFILE_SCOPE("Test");
        // t.scale = glm::vec3(0.2f);
        t.translation.y =
            sin(t.translation.x / 8.0f + t.translation.z / 8.0f + ren::timeSeconds() * 2.0f) * 2.0f;
      });
}

REN_PLUGIN("Test Plugin", test_plugin);



int main(int argc, char *argv[]) {
  try {
    ren::Application app("ren", {2560, 1440});

    auto cube = ren::MeshScene::load("assets/test/meshes/unit_cube.glb");
    int scale = 50;
    auto root = app.sceneLayer->scene.createEntity("root");
    for (int x = 0; x < scale; x++) {
      for (int z = 0; z < scale; z++) {
        auto e = app.sceneLayer->scene.createEntity().child_of(root).add<CubeWave>();

        ren::comp::Transform transform;
        transform.translation.x = (x - scale / 2) * 2.0f;
        transform.translation.y = 0;
        transform.translation.z = (z - scale / 2) * 2.0f;
        transform.scale = glm::vec3(0.75f);
        e.set<ren::comp::Transform>(transform);

        cube->instantiate(e);
      }
    }

    app.world.entity<ren::RenderDebug>();

    app.run();
  } catch (const std::exception &e) { fmt::print("Error: {}\n", e.what()); }

  return 0;
}
