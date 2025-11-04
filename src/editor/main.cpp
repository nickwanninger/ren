#include <iostream>
#include <vector>

#include <ren/core/Instrumentation.h>
#include <ren/core/Application.h>
#include <ren/renderer/graph/RenderGraph.h>

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



// A more complex deferred rendering pipeline example.
// Demonstrates: G-buffer rendering, depth pyramid, SSR, deferred lighting, composition.
ren::GraphHandle buildDeferredPipeline(ren::RenderGraph &G) {
  // === Stage 1: G-Buffer Rendering ===
  // Render scene geometry to multiple render targets
  auto gbufferAlbedo =
      G.createImage("gbuffer_albedo", {.scale = glm::vec2(1.0f)}, ren::GraphAccess::RenderTarget);
  auto gbufferNormal =
      G.createImage("gbuffer_normal", {.scale = glm::vec2(1.0f)}, ren::GraphAccess::RenderTarget);
  auto gbufferDepth =
      G.createImage("gbuffer_depth", {.scale = glm::vec2(1.0f)}, ren::GraphAccess::DepthTarget);
  auto gbufferMaterial =
      G.createImage("gbuffer_material", {.scale = glm::vec2(1.0f)}, ren::GraphAccess::RenderTarget);

  auto &gbufferPass = G.addTask("gbuffer_render", taskRunCallback);
  gbufferPass.write(gbufferAlbedo, ren::GraphAccess::RenderTarget);
  gbufferPass.write(gbufferNormal, ren::GraphAccess::RenderTarget);
  gbufferPass.write(gbufferDepth, ren::GraphAccess::DepthTarget);
  gbufferPass.write(gbufferMaterial, ren::GraphAccess::RenderTarget);

  // === Stage 2a: Depth Pyramid (for HZB-based techniques) ===
  auto depthPyramid = G.createImage("depth_pyramid", {.scale = glm::vec2(1.0f)},
                                    ren::GraphAccess::ComputeShaderWrite);
  auto &depthPyramidPass = G.addTask("compute_depth_pyramid", taskRunCallback);
  depthPyramidPass.read(gbufferDepth, ren::GraphAccess::ComputeShaderRead);
  depthPyramidPass.write(depthPyramid, ren::GraphAccess::ComputeShaderWrite);

  // === Stage 2b: Screen-Space Reflections (can run in parallel with depth pyramid) ===
  auto ssrResult =
      G.createImage("ssr_result", {.scale = glm::vec2(0.5f)}, ren::GraphAccess::ComputeShaderWrite);
  auto &ssrPass = G.addTask("compute_ssr", taskRunCallback);
  ssrPass.read(gbufferDepth, ren::GraphAccess::ComputeShaderRead);
  ssrPass.read(gbufferNormal, ren::GraphAccess::ComputeShaderRead);
  ssrPass.read(depthPyramid, ren::GraphAccess::ComputeShaderRead);  // depends on depth pyramid
  ssrPass.write(ssrResult, ren::GraphAccess::ComputeShaderWrite);

  // === Stage 3: Deferred Lighting (reads all G-buffers) ===
  auto litOutput =
      G.createImage("lit_output", {.scale = glm::vec2(1.0f)}, ren::GraphAccess::ComputeShaderWrite);
  auto &deferredLightingPass = G.addTask("compute_deferred_lighting", taskRunCallback);
  deferredLightingPass.read(gbufferAlbedo, ren::GraphAccess::ComputeShaderRead);
  deferredLightingPass.read(gbufferNormal, ren::GraphAccess::ComputeShaderRead);
  deferredLightingPass.read(gbufferMaterial, ren::GraphAccess::ComputeShaderRead);
  deferredLightingPass.write(litOutput, ren::GraphAccess::ComputeShaderWrite);

  // === Stage 4: Composition (combine lit result with reflections) ===
  auto compositeOutput = G.createImage("composite_output", {.scale = glm::vec2(1.0f)},
                                       ren::GraphAccess::RenderTarget);
  auto &compositionPass = G.addTask("composite_pass", taskRunCallback);
  compositionPass.read(litOutput, ren::GraphAccess::FragmentShaderRead);
  compositionPass.read(ssrResult, ren::GraphAccess::FragmentShaderRead);
  compositionPass.write(compositeOutput, ren::GraphAccess::RenderTarget);

  // === Stage 5: Post-processing (FXAA, film grain, etc.) ===
  auto postProcessOutput = G.createImage("postprocess_output", {.scale = glm::vec2(1.0f)},
                                         ren::GraphAccess::RenderTarget);
  auto &postProcessPass = G.addTask("post_process", taskRunCallback);
  postProcessPass.read(compositeOutput, ren::GraphAccess::FragmentShaderRead);
  postProcessPass.write(postProcessOutput, ren::GraphAccess::RenderTarget);

  // === Stage 6: Bloom Pass (Multi-scale downsampling + upsampling chain) ===
  // This demonstrates a common multi-pass technique using procedural graph construction.
  // Extract bright pixels, downsample to increasing scales, then upsample and accumulate back.

  const int bloomLevels = 4;  // Number of downsampling levels (1/2, 1/4, 1/8, 1/16)
  std::vector<ren::GraphHandle> bloomDownsamples;

  // Bright pass: extract luminance > threshold
  auto bloomBright = G.createImage("bloom_bright", {.scale = glm::vec2(1.0f)},
                                   ren::GraphAccess::ComputeShaderWrite);
  auto &bloomBrightPass = G.addTask("bloom_bright_pass", taskRunCallback);
  bloomBrightPass.read(postProcessOutput, ren::GraphAccess::ComputeShaderRead);
  bloomBrightPass.write(bloomBright, ren::GraphAccess::ComputeShaderWrite);

  // === Downsample chain: progressively shrink ===
  ren::GraphHandle currentDownsample = bloomBright;
  for (int level = 0; level < bloomLevels; ++level) {
    float scaleFactor = 1.0f / (2.0f * (1 << level));  // 0.5, 0.25, 0.125, 0.0625
    auto bloomDown = G.createImage("bloom_down", {.scale = glm::vec2(scaleFactor)},
                                   ren::GraphAccess::ComputeShaderWrite);
    auto &downsamplePass = G.addTask("bloom_downsample", taskRunCallback);
    downsamplePass.read(currentDownsample, ren::GraphAccess::ComputeShaderRead);
    downsamplePass.write(bloomDown, ren::GraphAccess::ComputeShaderWrite);

    bloomDownsamples.push_back(bloomDown);
    currentDownsample = bloomDown;
  }

  // === Upsample chain: progressively enlarge and accumulate ===
  ren::GraphHandle currentUpsample = currentDownsample;  // Start from smallest (1/16)
  for (int level = bloomLevels - 1; level >= 0; --level) {
    float scaleFactor = 1.0f / (2.0f * (1 << level));  // 0.125, 0.25, 0.5, 1.0
    auto bloomUp = G.createImage("bloom_up", {.scale = glm::vec2(scaleFactor)},
                                 ren::GraphAccess::ComputeShaderWrite);
    auto &upsamplePass = G.addTask("bloom_upsample", taskRunCallback);

    // Read the upsample source (previous level)
    upsamplePass.read(currentUpsample, ren::GraphAccess::ComputeShaderRead);

    // Read the residual from the corresponding downsample level for blending
    if (level > 0) {
      upsamplePass.read(bloomDownsamples[level - 1], ren::GraphAccess::ComputeShaderRead);
    } else {
      // Final upsample: blend with original bright pass
      upsamplePass.read(bloomBright, ren::GraphAccess::ComputeShaderRead);
    }

    upsamplePass.write(bloomUp, ren::GraphAccess::ComputeShaderWrite);

    currentUpsample = bloomUp;
  }

  auto bloomFinal = currentUpsample;  // The result of the final upsample is our final bloom

  // === Stage 7: Bloom Composition (blend bloom with post-processed image) ===
  auto bloomComposed = G.createImage("bloom_composed", {.scale = glm::vec2(1.0f)},
                                     ren::GraphAccess::RenderTarget);
  auto &bloomComposePass = G.addTask("bloom_composite", taskRunCallback);
  bloomComposePass.read(postProcessOutput, ren::GraphAccess::FragmentShaderRead);
  bloomComposePass.read(bloomFinal, ren::GraphAccess::FragmentShaderRead);
  bloomComposePass.write(bloomComposed, ren::GraphAccess::RenderTarget);

  // === Stage 8: Tonemapping & Final Output ===
  auto finalOutput =
      G.createImage("final_output", {.scale = glm::vec2(1.0f)}, ren::GraphAccess::RenderTarget);
  auto &tonemapPass = G.addTask("tonemap", taskRunCallback);
  tonemapPass.read(bloomComposed, ren::GraphAccess::FragmentShaderRead);
  tonemapPass.write(finalOutput, ren::GraphAccess::RenderTarget);

  return finalOutput;
}


int main(int argc, char *argv[]) {
  glm::uvec2 res;
  res.x = 1920;
  res.y = 1080;



  ren::Application app("Editor", res);

  // loadMeshIntoScene("/Users/nick/Downloads/tiny_isometric_room.glb", 0.01f);
  // loadMeshIntoScene("/Users/nick/Downloads/DamagedHelmet.glb", 1.0f);
  // loadMeshIntoScene("/Users/nick/Downloads/MetalRoughSpheres.glb", 1.0f);
  // loadMeshIntoScene("/Users/nick/Downloads/neighbourhood_city_modular_lowpoly.glb", 1.0f);
  // loadMeshIntoScene("/Users/nick/Downloads/NormalTangentTest.glb", 1.0f);
  // loadMeshIntoScene("/Users/nick/Downloads/NormalTangentMirrorTest.glb", 1.0f);

  // loadMeshIntoScene("/Users/nick/Desktop/sponza.glb");
  // loadMeshIntoScene("/Users/nick/Desktop/RamenCup.glb");
  loadMeshIntoScene("assets/test/meshes/simple_scene.glb");
  // loadMeshIntoScene("assets/test/meshes/unit_cube.glb");
  // loadMeshIntoScene("/Users/nick/dev/kajiya/assets/meshes/viziers_observation_deck/scene.gltf",
  // 0.01);

  // loadMeshIntoScene(
  //     "/Users/nick/dev/kajiya/assets/meshes/flying_world_-_battle_of_the_trash_god/scene.gltf",
  //     0.002f);
  // loadMeshIntoScene("/Users/nick/Desktop/enrico.glb");
  // loadMeshIntoScene("/Users/nick/Downloads/main_sponza/NewSponza_Main_glTF_003.gltf");
  // loadMeshIntoScene("/Users/nick/Downloads/pkg_a_curtains/NewSponza_Curtains_glTF.gltf");
  // loadMeshIntoScene("/Users/nick/Downloads/pkg_b_ivy/NewSponza_IvyGrowth_glTF.gltf");
  // loadMeshIntoScene("/Users/nick/Downloads/pkg_c_trees/NewSponza_CypressTree_glTF.gltf");

  // loadMeshIntoScene("/Users/nick/Downloads/huge_icelandic_lava_cliff_sieoz_high.glb");
  // loadMeshIntoScene("/Users/nick/Downloads/Rock.glb");
  // loadMeshIntoScene("/Users/nick/Downloads/broken_wall_slunl_high.glb");
  // loadMeshIntoScene("/Users/nick/Downloads/broken_stump_rkswd_raw.glb");

  app.run();

  return 0;
}
