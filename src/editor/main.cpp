#include <iostream>
#include <vector>

#include <ren/core/Instrumentation.h>
#include <ren/core/Application.h>
#include <ren/renderer/graph/RenderGraph.h>

#include <ren/core/Flag.h>

ren::Flag<std::string> loadArg("load", "assets/test/meshes/simple_scene.glb",
                               "Path to a mesh to load at startup");


ren::Flag<float> scaleArg("load-scale", 1.0f, "Uniform scale to apply to the loaded mesh");


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


static void taskRunCallback(ren::GraphRunContext &ctx) {
  fmt::println("Running {}", ctx.task->name());
}



int main(int argc, char *argv[]) {
  ren::parseFlags(argc, argv);
  glm::uvec2 res;
  res.x = 1920;
  res.y = 1080;




  ren::Application app("Editor", res);

  // auto &vulkan = ren::getVulkan();
  // print_descriptor_indexing_limits(vulkan.physical_device);
  // return 0;

  if (loadArg.get() == "SPONZA") {
    loadMeshIntoScene("/Users/nick/Downloads/main_sponza/NewSponza_Main_glTF_003.gltf",
                      scaleArg.get());
    loadMeshIntoScene("/Users/nick/Downloads/pkg_a_curtains/NewSponza_Curtains_glTF.gltf",
                      scaleArg.get());
    loadMeshIntoScene("/Users/nick/Downloads/pkg_b_ivy/NewSponza_IvyGrowth_glTF.gltf",
                      scaleArg.get());
  } else {
    loadMeshIntoScene(loadArg.get().c_str(), scaleArg.get());
  }


  app.run();

  return 0;
}
