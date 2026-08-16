
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
ren::Flag<bool> runSaxpyArg("run-saxpy", false, "Run the synchronous Slang/BDA SAXPY smoke test before opening the editor");
ren::Flag<bool> runImageHeapArg("run-imageheap", false, "Run the bindless image heap smoke test before opening the editor");


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
  auto x = TypedBuffer<float>(length, BufferDomain::Upload, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  auto y = TypedBuffer<float>(length, BufferDomain::Upload, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  auto out = TypedBuffer<float>(length, BufferDomain::Readback, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  randomize(x, length);
  randomize(y, length);

  auto program = ren::make<ren::ShaderProgram>("test/saxpy");
  auto cmd = unit.begin();
  auto compute = cmd->bindCompute(program);
  auto pushConstants = compute.pushConstant("pushConstants");
  pushConstants.set("a", a)
      .set("length", length)
      .set("x", x.devicePointer<float>())
      .set("y", y.devicePointer<float>())
      .set("output", out.devicePointer<float>());
  compute.dispatch({(length + 255) / 256, 1, 1});
  unit.submitTo(*getVulkan().graphicsQueue)->awaitCompletion();

  // Validate the result on the CPU
  auto* mappedX = x.hostData();
  auto* mappedY = y.hostData();
  auto* mappedOut = out.hostData();
  for (u32 i = 0; i < length; i++) {
    float expected = a * mappedX[i] + mappedY[i];
    REN_ASSERT(fabs(mappedOut[i] - expected) < 0.001f);
  }
  ren::println("SAXPY validation passed for {} elements", length);
}

// #include <ren/core/ThreadPool.h>
#include <ren/renderer/DescriptorHeap.h>

void imageHeapSampleTest() {
  constexpr u32 size = 2;
  auto img = ren::ImageBuilder("image heap sample").setSize(size).setFormat(VK_FORMAT_R8G8B8A8_UNORM).build();

  std::array<u8, size * size * 4> pixels;
  for (u32 pixel = 0; pixel < size * size; ++pixel) {
    pixels[pixel * 4 + 0] = 128;
    pixels[pixel * 4 + 1] = 128;
    pixels[pixel * 4 + 2] = 128;
    pixels[pixel * 4 + 3] = 255;
  }
  img->uploadPixels(pixels.data());
  auto program = ren::make<ren::ShaderProgram>("test/imageheap");



  auto outBuffer = TypedBuffer<glm::vec4>(1, BufferDomain::Readback, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

  SubmissionUnit unit;

  for (int i = 0; i < 15; i++) {
    auto start = std::chrono::high_resolution_clock::now();
    auto cmd = unit.begin();

    auto compute = cmd->bindCompute(program);
    auto args = compute.pushConstant("pc");

    auto& renderer = Renderer::get();
    auto sampler = renderer.getSamplerCache().get(SamplerDesc{});

    args.set("image", img).set("sampler", sampler).set("out", outBuffer.devicePointer<glm::vec4>());

    compute.dispatch({1, 1, 1});  // Fire off the compute shader.

    // Submit, and wait for completion
    unit.submitTo(*getVulkan().computeQueue)->awaitCompletion();
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    ren::println("Image heap sample test completed in {} microseconds", duration);
  }


  // Validate the result
  const glm::vec4 result = *outBuffer.hostData();
  ren::println("Image heap sample passed: [{}, {}, {}, {}]", result.x, result.y, result.z, result.w);
}

int main(int argc, char* argv[]) {
  ren::parseFlags(argc, argv);

  REN_PROFILE_BEGIN_SESSION("profile", "profile.json");

  glm::uvec2 res;
  res.x = 1920;
  res.y = 1080;

  ren::Application app("Editor", res);


  if (loadArg.get() != "") {
    loadMeshIntoScene(loadArg.get().c_str(), scaleArg.get());
  }


  // if (runSaxpyArg.get()) {
  //   testSaxpy();
  //   return 0;
  // }

  // if (runImageHeapArg.get()) {
  //   imageHeapSampleTest();
  //   return 0;
  // }

  app.run();
  REN_PROFILE_END_SESSION();

  return 0;
}
