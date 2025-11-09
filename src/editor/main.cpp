#include <iostream>
#include <vector>

#include <ren/core/Instrumentation.h>
#include <ren/core/Application.h>
#include <ren/renderer/graph/RenderGraph.h>

#include <ren/core/Flag.h>

ren::Flag<std::string> loadArg("load", "assets/test/meshes/simple_scene.glb", "Path to a mesh to load at startup");

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

  loadMeshIntoScene(loadArg.get().c_str());

  app.run();

  return 0;
}
