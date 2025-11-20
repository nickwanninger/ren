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


static void addLight(glm::vec3 position, glm::vec3 color, float intensity, float radius) {
  auto &world = ren::world();
  auto entity = world.entity();
  entity.set<ren::comp::Transform>(ren::comp::Transform{position});
  ren::PointLightComponent plc;
  plc.color = color;
  plc.intensity = intensity;
  plc.radius = radius;
  entity.set<ren::PointLightComponent>(plc);
}



#include <shaderc/shaderc.hpp>

void compileHLSLTest() {
  // Use shaderc to compile hlsl with multiple entry points to multiple spir-v modules

  shaderc::Compiler compiler;
  shaderc::CompileOptions options;
  options.SetSourceLanguage(shaderc_source_language_hlsl);
  options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
  options.SetOptimizationLevel(shaderc_optimization_level_performance);


  options.SetPreserveBindings(true);
  options.SetAutoMapLocations(true);
  options.SetGenerateDebugInfo();  // Preserve debug info for reflection
  options.SetOptimizationLevel(shaderc_optimization_level_performance);


  const char *hlslSource = R"(
    struct VSInput {
      float3 position : POSITION;
      float3 normal : NORMAL;
    };

    struct PSInput {
      float4 position : SV_POSITION;
      float3 normal : NORMAL;
    };

    PSInput mainVS(VSInput input) {
      PSInput output;
      output.position = float4(input.position, 1.0);
      output.normal = input.normal;
      return output;
    }

    float4 mainPS(PSInput input) : SV_TARGET {
      return float4(abs(input.normal), 1.0);
    }
  )";

  // Compile vertex shader
  auto vertResult = compiler.CompileGlslToSpv(hlslSource, strlen(hlslSource), shaderc_vertex_shader,
                                              "shader.hlsl", "mainVS", options);
  if (vertResult.GetCompilationStatus() != shaderc_compilation_status_success) {
    fmt::println("Vertex shader compilation failed: {}", vertResult.GetErrorMessage());
    return;
  }

  // Compile pixel shader
  auto fragResult = compiler.CompileGlslToSpv(
      hlslSource, strlen(hlslSource), shaderc_fragment_shader, "shader.hlsl", "mainPS", options);
  if (fragResult.GetCompilationStatus() != shaderc_compilation_status_success) {
    fmt::println("Fragment shader compilation failed: {}", fragResult.GetErrorMessage());
    return;
  }
}

int main(int argc, char *argv[]) {
  ren::parseFlags(argc, argv);


  compileHLSLTest();
  return 0;

  glm::uvec2 res;
  res.x = 1920;
  res.y = 1080;

  ren::Application app("Editor", res);




  int numLights = 16;
  float lightRadius = 8.0f;
  // add lights in a ring around the origin
  for (int i = 0; i < numLights; i++) {
    float angle = (float)i / (float)numLights * 2.0f * glm::pi<float>();
    float x = cos(angle) * lightRadius;
    float z = sin(angle) * lightRadius;
    addLight(glm::vec3(x, 3.0f, z),
             glm::vec3(0.5f + 0.5f * cos(angle), 0.5f + 0.5f * sin(angle), 1.0f), 1.0f,
             lightRadius * 0.8f);
  }


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
