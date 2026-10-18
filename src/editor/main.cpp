
#include <ren/core/Instrumentation.h>
#include <ren/core/Application.h>
#include <ren/renderer/graph/RenderGraph.h>
#include <ren/renderer/shader/ShaderReflection.h>

#include <ren/core/Flag.h>
#include <chrono>
#include <ren/assets/MeshScene.hpp>
#include "ren/assets/AssetSource.h"
#include <ren/core/ThreadPool.h>
#include "ren/renderer/Buffer.h"
#include "ren/renderer/shader/ShaderCache.h"

#include <ren/core/Systems.h>
#include <ren/core/Components.h>
#include <ren/renderer/submission/SubmissionUnit.h>
#include <ren/core/Result.h>

#include "EditorWindow.h"


using namespace ren;

ren::Flag<std::string> loadArg("load", "", "Path to a mesh to load at startup");
ren::Flag<float> scaleArg("load-scale", 1.0f, "Uniform scale to apply to the loaded mesh");
ren::Flag<bool> runSaxpyArg("run-saxpy", false, "Run the synchronous Slang/BDA SAXPY smoke test before opening the editor");
ren::Flag<bool> runImageHeapArg("run-imageheap", false, "Run the bindless image heap smoke test before opening the editor");
ren::Flag<bool> instrumentArg("instrument", false, "Enable instrumentation and profiling");


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
  SubmissionUnit unit;
  for (int i = 0; i < 1000; i++) {
    constexpr u32 length = 10;
    float a = 2.5f;
    auto x = TypedBuffer<float>(length, BufferDomain::Upload, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    auto y = TypedBuffer<float>(length, BufferDomain::Upload, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    auto out = TypedBuffer<float>(length, BufferDomain::Readback, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    // {
    //   REN_PROFILE_SCOPE("Randomize");
    //   randomize(x, length);
    //   randomize(y, length);
    // }
    REN_PROFILE_SCOPE("SAXPY");

    auto program = ren::getShader("test/saxpy");
    auto cmd = unit.begin();
    auto compute = cmd->bindCompute(program);
    auto pushConstants = compute.root("pushConstants");
    pushConstants.set("a", a)
        .set("length", length)
        .set("x", x.devicePointer<float>())
        .set("y", y.devicePointer<float>())
        .set("output", out.devicePointer<float>());

    compute.dispatch({(length + 255) / 256, 1, 1});
    compute.writeBarrier();

    auto f = unit.submitTo(*getVulkan().computeQueue);
    {
      REN_PROFILE_SCOPE("Wait");
      f->awaitCompletion();
    }
  }


  // // Validate the result on the CPU
  // auto *mappedX = x.hostData();
  // auto *mappedY = y.hostData();
  // auto *mappedOut = out.hostData();
  // for (u32 i = 0; i < length; i++) {
  //   float expected = a * mappedX[i] + mappedY[i];
  //   REN_ASSERT(fabs(mappedOut[i] - expected) < 0.001f);
  // }
}


#include <ren/assets/AssetStore.h>

int main(int argc, char* argv[]) {
  // ren::FilesystemAssetSource source("assets");
  // source.mkdir("store");
  // ren::AssetStore store(source);

  // store.storeBlob("Hello", {{"name", "test.asset"}, {"type", "mesh"}, {"source", "test.glb"}}, "a-message");

  // return 0;
  ren::parseFlags(argc, argv);


  if (instrumentArg.get()) {
    REN_PROFILE_BEGIN("profile");
  }


  glm::uvec2 res;
  res.x = 1920;
  res.y = 1080;

  ren::Application app("Editor", res, ren::makeBox<editor::EditorWindow>());



  // if (loadArg.get() != "") {
  //   loadMeshIntoScene(loadArg.get().c_str(), scaleArg.get());
  // }

  // loadMeshIntoScene("/Users/nick/dev/Models/sponza.glb");
  // loadMeshIntoScene("/Users/nick/dev/Models/thinker.glb", 1);

  ren::getAssetManager().importMesh("/Users/nick/dev/Models/sponza.glb");
  // ren::getAssetManager().importMesh("assets/test/meshes/unit_cube.glb");


  if (runSaxpyArg.get()) {
    testSaxpy();
    return 0;
  }


  return 0;

  ren::backgroundWork([&]() {
    while (app.isRunning()) {
      REN_PROFILE_SCOPE("Background work thread");
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
  });

  app.run();


  REN_PROFILE_END_SESSION();

  return 0;
}
