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
#include <ren/scripting/Scheme.h>

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

// extern "C" {
#include <ren/scripting/scheme/s7.h>
#include <ren/scripting/Console.h>
#include <ren/scripting/EcsBindings.h>


/* C function exposed to Scheme: (add2 a b) -> a+b */
static s7_pointer add2(s7_scheme *s7, s7_pointer args) {
  /* extract two args from the proper list */
  s7_pointer a = s7_car(args);
  s7_pointer b = s7_cadr(args);

  /* type checks (be strict; fail fast) */
  if (!s7_is_number(a) || !s7_is_number(b)) {
    return s7_error(s7, s7_make_symbol(s7, "wrong-type-arg"),
                    s7_list(s7, 2, s7_make_string(s7, "add2: expected two numbers"),
                            s7_cons(s7, a, s7_cons(s7, b, s7_nil(s7)))));
  }

  /* convert and compute (works for ints/reals) */
  const double da = s7_number_to_real(s7, a);
  const double db = s7_number_to_real(s7, b);
  return s7_make_real(s7, da + db);
}

int main(int argc, char *argv[]) {
  ren::Scheme vm;
  struct {
    int a;
    int b;
  } foo;
  foo.a = 42;
  foo.b = 84;

  auto p = vm.makePointer(&foo);
  fmt::println("Pointer: {}", (void *)p.asPointer());
  vm.define("foo", p);

  // return 0;

  // ren::SchemeConsole console(vm);


  // sexp_scheme_init();

  // sexp ctx = sexp_make_eval_context(NULL, NULL, NULL, 0, 0);

  // SCM_CHK(sexp_load_standard_env(ctx, NULL, SEXP_SEVEN));
  // SCM_CHK(sexp_load_standard_ports(ctx, NULL, stdin, stdout, stderr, 1));


  // SCM_CHK(sexp_eval_string(ctx, "(display 1)", -1, NULL));
  // SCM_CHK(sexp_load(ctx, sexp_c_string(ctx, "hello.scm", -1), NULL));

  // sexp_destroy_context(ctx);

  // return 0;

  try {
    ren::Application app("ren", {1920, 1080});

    // ren::registerEcsBindings(vm, app.world);
    // vm.load("hello.scm");
    // ren::system::onUpdate("Scheme").run([&](flecs::iter &it) {
    //   console.Draw("Scheme Console");
    // });


    // auto e = ren::createEntity();
    // add a directional light component
    // e.add<ren::comp::DirectionalLight>();

    // loadMeshIntoScene("/Users/nick/dev/kajiya/assets/meshes/viziers_observation_deck/scene.gltf",
    // 0.01);
    // loadMeshIntoScene("/Users/nick/dev/kajiya/assets/meshes/flying_world_-_battle_of_the_trash_god/scene.gltf",
    // 0.002f);

    loadMeshIntoScene("/Users/nick/Desktop/sponza.glb");
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


    // auto cube = ren::MeshScene::load("assets/test/meshes/unit_cube.glb");
    // auto e = cube->instantiate({});
    // e.add<DebugLineDraw>();

    // int scale = 50;
    // auto root = app.sceneLayer->scene.createEntity("root");
    // for (int x = 0; x < scale; x++) {
    //   for (int z = 0; z < scale; z++) {
    //     auto e = app.sceneLayer->scene.createEntity().child_of(root).add<CubeWave>();

    //     ren::comp::Transform transform;
    //     transform.translation.x = (x - scale / 2) * 2.0f;
    //     transform.translation.y = 0;
    //     transform.translation.z = (z - scale / 2) * 2.0f;
    //     transform.scale = glm::vec3(0.75f);
    //     e.set<ren::comp::Transform>(transform);
    //     cube->instantiate(e);
    //   }
    // }

    app.run();
  } catch (const std::exception &e) { fmt::print("Error: {}\n", e.what()); }

  return 0;
}
