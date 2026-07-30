
#include <ren/core/Instrumentation.h>
#include <ren/core/Application.h>
#include <ren/renderer/graph/RenderGraph.h>
#include <ren/renderer/shader/ShaderReflection.h>

#include <ren/core/Flag.h>
#include <ren/assets/MeshScene.hpp>
#include "ren/renderer/Buffer.h"
#include "ren/renderer/shader/ShaderProgram.h"

#include <ren/core/Systems.h>
#include <ren/core/Components.h>
#include <ren/renderer/submission/SubmissionUnit.h>
#include <ren/core/Result.h>


using namespace ren;

ren::Flag<std::string> loadArg("load", "assets/test/meshes/simple_scene.glb", "Path to a mesh to load at startup");
ren::Flag<float> scaleArg("load-scale", 1.0f, "Uniform scale to apply to the loaded mesh");
ren::Flag<bool> runSaxpyArg(
    "run-saxpy", false,
    "Run the synchronous Slang/BDA SAXPY smoke test before opening the editor");


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



void testCalibration(void) {
  SubmissionUnit unit;


  for (int i = 0; i < 1000; i++) {
    auto cmd = unit.begin();

    auto tik = cmd->beginTimestampQuery("Calibration");
    cmd->endTimestampQuery(tik);

    auto f = unit.submitTo(*getVulkan().computeQueue);
    f->awaitCompletion();
  }
}

void testSaxpy(void) {
  using namespace ren;

  SubmissionUnit unit;
  constexpr u32 length = 10000;

  auto randomize = [](BufferMemory& buffer, u32 count) {
    auto* mapped = buffer.hostData<float>();
    for (u32 i = 0; i < count; i++) {
      mapped[i] = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
    }
  };

  float a = 2.5f;
  auto x = allocateBuffer<float>(length, BufferDomain::Upload);
  auto y = allocateBuffer<float>(length, BufferDomain::Upload);
  auto out = allocateBuffer<float>(length, BufferDomain::Readback);
  randomize(x, length);
  randomize(y, length);

  auto program = ren::make<ren::ShaderProgram>("test/saxpy");
  auto cmd = unit.begin();
  auto cursor = cmd->bindCompute(program);
  auto pushConstants = cursor.pushConstant("pushConstants");
  pushConstants
      .set("a", a)
      .set("length", length)
      .set("x", x.devicePointer<float>())
      .set("y", y.devicePointer<float>())
      .set("output", out.devicePointer<float>());
  cmd->dispatch(cursor, {(length + 255) / 256, 1, 1});
  unit.submitTo(*getVulkan().graphicsQueue)->awaitCompletion();

  // Validate the result on the CPU
  auto* mappedX = x.hostData<float>();
  auto* mappedY = y.hostData<float>();
  auto* mappedOut = out.hostData<float>();
  for (u32 i = 0; i < length; i++) {
    float expected = a * mappedX[i] + mappedY[i];
    REN_ASSERT(fabs(mappedOut[i] - expected) < 0.001f);
  }
  ren::println("SAXPY validation passed for {} elements", length);
}

// #include <ren/core/ThreadPool.h>

int main(int argc, char* argv[]) {
  ren::parseFlags(argc, argv);

  REN_PROFILE_BEGIN_SESSION("profile", "profile.json");

  glm::uvec2 res;
  res.x = 1920;
  res.y = 1080;


  ren::Application app("Editor", res);

  if (runSaxpyArg.get()) {
    testSaxpy();
    return 0;
  }

  // if (loadArg.get() == "SPONZA") {
  //   loadMeshIntoScene("/Users/nick/Downloads/main_sponza/NewSponza_Main_glTF_003.gltf", scaleArg.get());
  //   loadMeshIntoScene("/Users/nick/Downloads/pkg_a_curtains/NewSponza_Curtains_glTF.gltf", scaleArg.get());
  //   loadMeshIntoScene("/Users/nick/Downloads/pkg_b_ivy/NewSponza_IvyGrowth_glTF.gltf", scaleArg.get());
  // } else {
  //   loadMeshIntoScene(loadArg.get().c_str(), scaleArg.get());
  // }

  app.run();
  REN_PROFILE_END_SESSION();

  return 0;
}
