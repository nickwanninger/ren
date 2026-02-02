
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
#include <ren/renderer/shader/ParameterBinding.h>
#include <ren/renderer/submission/SubmissionUnit.h>
#include <ren/core/Result.h>


using namespace ren;

ren::Flag<std::string> loadArg("load", "assets/test/meshes/simple_scene.glb", "Path to a mesh to load at startup");
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

  u64 length = 10000;


  auto randomize = [](auto buffer, u64 length) {
    auto* mapped = buffer->map();
    for (u64 i = 0; i < length; i++) {
      mapped[i] = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
    }
    buffer->unmap();
  };

  float a = 2.5f;
  auto x = ren::make<ren::StorageBuffer<float>>(length);
  auto y = ren::make<ren::StorageBuffer<float>>(length);
  randomize(x, length);
  randomize(y, length);

  auto out = ren::make<ren::StorageBuffer<float>>(length);

  auto program = ren::make<ren::ShaderProgram>("test/saxpy");


  for (int i = 0; i < 1000; i++) {
    auto cmd = unit.begin();

    auto tik = cmd->beginTimestampQuery("SAXPY Compute");
    ren::ShaderObject& obj = unit.createShaderObject(program);
    auto blk = obj.block("params");
    blk.field("a").set<float>(a);
    blk.field("x").bind(x);
    blk.field("y").bind(y);
    blk.field("out").bind(out);

    cmd->dispatchCompute(obj, {length / 1024 + 1, 1, 1});

    cmd->endTimestampQuery(tik);

    // now we submit the command buffer and wait for it to complete.
    auto f = unit.submitTo(*getVulkan().computeQueue);
    f->awaitCompletion();
  }


  // Validate the result on the CPU
  auto* mappedX = x->map();
  auto* mappedY = y->map();
  auto* mappedOut = out->map();
  for (u64 i = 0; i < length; i++) {
    // printf("i=%llu: %f * %f + %f = %f\n", i, a, mappedX[i], mappedY[i], mappedOut[i]);
    float expected = a * mappedX[i] + mappedY[i];
    REN_ASSERT(fabs(mappedOut[i] - expected) < 0.001f);
  }
  x->unmap();
  y->unmap();
  out->unmap();
}

// #include <ren/core/ThreadPool.h>

int main(int argc, char* argv[]) {
  ren::parseFlags(argc, argv);

  REN_PROFILE_BEGIN_SESSION("profile", "profile.json");

  glm::uvec2 res;
  res.x = 1920;
  res.y = 1080;


  ren::Application app("Editor", res);



  testSaxpy();
  // exit(0);

#if 0

  // --- Expected Compute Shader ---
  auto program = ren::make<ren::ShaderProgram>("test/compute");

  u64 length = 2560 * 1440;

  u64 threadsInWokrgroup = 1000;
  auto outputBuffer = ren::make<ren::StorageBuffer<float>>(length);

  ren::SubmissionUnit unit;
  auto start = std::chrono::high_resolution_clock::now();

  long count = 1000;
  for (long i = 0; i < count; i++) {
    auto cmd = unit.begin();

    ren::ShaderObject& obj = unit.createShaderObject(program);

    float expectedValue = i;

    auto blk = obj.block("test");
    blk["value"].set<float>(expectedValue);
    blk["outputBuffer"].bind(outputBuffer);

    cmd->dispatchCompute(obj, {length / threadsInWokrgroup, 1, 1});

    // now we submit the command buffer and wait for it to complete.
    auto f = unit.submitTo(*getVulkan().computeQueue);
    f->awaitCompletion();



    // Read back the output buffer.
    // auto* mapped = outputBuffer->map();
    // for (u64 i = 0; i < length; i++) {
    //   auto value = mapped[i];
    //   REN_ASSERT(mapped[i] == expectedValue);
    // }
    // outputBuffer->unmap();


    // yay!
  }
  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
  float average = duration / (float)count;
  ren::println("average: {:.3f} ms, total: {:.3f} ms", average / 1000.0 / 1000.0, duration / 1000.0 / 1000.0);
  // ---------------------------
#endif
  // exit(0);

  if (loadArg.get() == "SPONZA") {
    loadMeshIntoScene("/Users/nick/Downloads/main_sponza/NewSponza_Main_glTF_003.gltf", scaleArg.get());
    loadMeshIntoScene("/Users/nick/Downloads/pkg_a_curtains/NewSponza_Curtains_glTF.gltf", scaleArg.get());
    loadMeshIntoScene("/Users/nick/Downloads/pkg_b_ivy/NewSponza_IvyGrowth_glTF.gltf", scaleArg.get());
  } else {
    loadMeshIntoScene(loadArg.get().c_str(), scaleArg.get());
  }


  app.run();
  REN_PROFILE_END_SESSION();

  return 0;
}
