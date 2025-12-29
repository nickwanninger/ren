
#include <ren/core/Instrumentation.h>
#include <ren/core/Application.h>
#include <ren/renderer/graph/RenderGraph.h>
#include <ren/renderer/shader/ShaderReflection.h>

#include <ren/core/Flag.h>
#include <ren/renderer/shader/SlangCompiler.h>
#include <ren/assets/MeshScene.hpp>

#include <ren/core/Systems.h>
#include <ren/core/Components.h>


ren::Flag<std::string> loadArg("load", "assets/test/meshes/simple_scene.glb",
                               "Path to a mesh to load at startup");


ren::Flag<float> scaleArg("load-scale", 1.0f, "Uniform scale to apply to the loaded mesh");


void loadMeshIntoScene(const char* path, float scaleChange = 0.0f) {
  ren::println("Loading {}...", path);
  auto mesh = ren::MeshScene::load(path);
  if (!mesh) {
    ren::errln("Failed to load mesh from {}", path);
    return;
  }

  auto entity = mesh->instantiate({});
  if (scaleChange != 0.0f) {
    entity.get_mut<ren::comp::Transform>().scale = glm::vec3(scaleChange);
  }
}


int main(int argc, char* argv[]) {
  ren::parseFlags(argc, argv);

  REN_PROFILE_BEGIN_SESSION("profile", "profile.json");

  glm::uvec2 res;
  res.x = 1920;
  res.y = 1080;

  ren::Application app("Editor", res);

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
  REN_PROFILE_END_SESSION();

  return 0;
}
