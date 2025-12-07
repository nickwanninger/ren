#include <iostream>
#include <vector>

#include <ren/core/Instrumentation.h>
#include <ren/core/Application.h>
#include <ren/renderer/graph/RenderGraph.h>
#include <ren/renderer/ShaderReflection.h>

#include <ren/core/Flag.h>

ren::Flag<std::string> loadArg("load", "assets/test/meshes/simple_scene.glb",
                               "Path to a mesh to load at startup");


ren::Flag<float> scaleArg("load-scale", 1.0f, "Uniform scale to apply to the loaded mesh");


void loadMeshIntoScene(const char* path, float scaleChange = 0.0f) {
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

static void taskRunCallback(ren::GraphRunContext& ctx) {
  fmt::println("Running {}", ctx.task->name());
}


static void addLight(glm::vec3 position, glm::vec3 color, float intensity, float radius) {
  auto& world = ren::world();
  auto entity = world.entity();
  entity.set<ren::comp::Transform>(ren::comp::Transform{position});
  ren::PointLightComponent plc;
  plc.color = color;
  plc.intensity = intensity;
  plc.radius = radius;
  entity.set<ren::PointLightComponent>(plc);
}



#include "slang.h"
#include "slang-com-ptr.h"
#include "slang-com-helper.h"




struct CompiledShader {
  std::string name;  // Entry point name
  std::vector<u8> spirv;
  VkShaderStageFlagBits stage;
  uint32_t parameterCount = 0;  // Number of entry point parameters

  // Keep the linked program alive (owns the layout and other reflection data)
  Slang::ComPtr<slang::IComponentType> linkedProgram;

  // Convenience accessor - valid as long as linkedProgram is alive
  slang::ProgramLayout* getLayout() const {
    return linkedProgram ? linkedProgram->getLayout() : nullptr;
  }
};


// Helper to convert Slang stage to Vulkan stage
VkShaderStageFlagBits slangStageToVulkan(SlangStage stage) {
  switch (stage) {
    case SLANG_STAGE_VERTEX: return VK_SHADER_STAGE_VERTEX_BIT;
    case SLANG_STAGE_FRAGMENT: return VK_SHADER_STAGE_FRAGMENT_BIT;
    case SLANG_STAGE_COMPUTE: return VK_SHADER_STAGE_COMPUTE_BIT;
    case SLANG_STAGE_GEOMETRY: return VK_SHADER_STAGE_GEOMETRY_BIT;
    case SLANG_STAGE_HULL: return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    case SLANG_STAGE_DOMAIN: return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    default: throw std::runtime_error("Unsupported shader stage");
  }
}



// Helper to check diagnostics
void checkDiagnostics(slang::IBlob* diagnosticsBlob) {
  if (diagnosticsBlob && diagnosticsBlob->getBufferSize() > 0) {
    const char* msg = (const char*)diagnosticsBlob->getBufferPointer();
    throw std::runtime_error(std::string("Slang compilation error: ") + msg);
  }
}


Slang::ComPtr<slang::IGlobalSession> globalSession;
Slang::ComPtr<slang::ISession> session;

std::vector<CompiledShader> compileSlangShaders(const char* filePath) {
  std::vector<CompiledShader> results;

  // 1. Create global session
  if (SLANG_FAILED(createGlobalSession(globalSession.writeRef()))) {
    throw std::runtime_error("Failed to create Slang global session");
  }

  // 2. Configure session for SPIR-V target
  slang::SessionDesc sessionDesc = {};
  slang::TargetDesc targetDesc = {};
  targetDesc.format = SLANG_SPIRV;
  targetDesc.profile = globalSession->findProfile("spirv_1_5");

  // Enable Vulkan reflection to get parameter information
  slang::CompilerOptionEntry reflectionOption = {};
  reflectionOption.name = slang::CompilerOptionName::VulkanEmitReflection;
  reflectionOption.value.kind = slang::CompilerOptionValueKind::Int;
  reflectionOption.value.intValue0 = 1;

  std::vector<slang::CompilerOptionEntry> compilerOptions;
  compilerOptions.push_back(reflectionOption);

  targetDesc.compilerOptionEntries = compilerOptions.data();
  targetDesc.compilerOptionEntryCount = static_cast<SlangInt>(compilerOptions.size());

  sessionDesc.targets = &targetDesc;
  sessionDesc.targetCount = 1;

  if (SLANG_FAILED(globalSession->createSession(sessionDesc, session.writeRef()))) {
    throw std::runtime_error("Failed to create Slang session");
  }

  // 3. Load module from file
  Slang::ComPtr<slang::IBlob> diagnosticsBlob;
  Slang::ComPtr<slang::IModule> module;
  module = session->loadModule(filePath, diagnosticsBlob.writeRef());

  if (!module) {
    checkDiagnostics(diagnosticsBlob);
    throw std::runtime_error("Failed to load Slang module");
  }


  // 4. Find all entry points in the module
  SlangInt32 entryPointCount = module->getDefinedEntryPointCount();
  if (entryPointCount == 0) { throw std::runtime_error("No entry points found in shader source"); }


  {
    std::vector<slang::IComponentType*> comps;
    for (SlangInt32 i = 0; i < entryPointCount; ++i) {
      Slang::ComPtr<slang::IEntryPoint> entryPoint;
      if (SLANG_FAILED(module->getDefinedEntryPoint(i, entryPoint.writeRef()))) {
        throw std::runtime_error("Failed to get entry point");
      }
      comps.push_back(entryPoint);
    }


    Slang::ComPtr<slang::IComponentType> program;
    if (SLANG_FAILED(session->createCompositeComponentType(
            comps.data(), comps.size(), program.writeRef(), diagnosticsBlob.writeRef()))) {
      checkDiagnostics(diagnosticsBlob);
      throw std::runtime_error("Failed to compose program");
    }

    // 7. Link the program
    Slang::ComPtr<slang::IComponentType> linkedProgram;
    if (SLANG_FAILED(program->link(linkedProgram.writeRef(), diagnosticsBlob.writeRef()))) {
      checkDiagnostics(diagnosticsBlob);
      throw std::runtime_error("Failed to link program");
    }


    ren::ShaderReflection sr;
    sr.parseFromSlang(linkedProgram->getLayout());
    std::cout << "full module reflection: " << sr.getRoot()->toJson().dump(2) << std::endl;


    for (SlangInt32 i = 0; i < entryPointCount; ++i) {
      Slang::ComPtr<slang::IBlob> spirvCode;
      if (SLANG_FAILED(linkedProgram->getEntryPointCode(
              i,  // entry point index (we only have one in this composed program)
              0,  // target index
              spirvCode.writeRef(), diagnosticsBlob.writeRef()))) {
        checkDiagnostics(diagnosticsBlob);
        throw std::runtime_error("Failed to get entry point code");
      }
      fmt::println("entry point {} SPIR-V size: {}", i, spirvCode->getBufferSize());

      // FILE* p = popen("spirv-dis --stdin --validate", "w");

      // fwrite(spirvCode->getBufferPointer(), 1, spirvCode->getBufferSize(), p);
      // pclose(p);



      // FILE *f = fopen(fmt::format("output_entrypoint_{}.spv", i).c_str(), "wb");
      // fwrite(spirvCode->getBufferPointer(), 1, spirvCode->getBufferSize(), f);
      // fclose(f);
    }
  }

  return {};



  // 5. Compile each entry point
  for (SlangInt32 i = 0; i < entryPointCount; ++i) {
    Slang::ComPtr<slang::IEntryPoint> entryPoint;
    if (SLANG_FAILED(module->getDefinedEntryPoint(i, entryPoint.writeRef()))) {
      throw std::runtime_error("Failed to get entry point");
    }

    // 6. Compose the program (module + entry point)
    std::array<slang::IComponentType*, 2> componentTypes = {module, entryPoint};

    Slang::ComPtr<slang::IComponentType> program;
    if (SLANG_FAILED(session->createCompositeComponentType(
            componentTypes.data(), componentTypes.size(), program.writeRef(),
            diagnosticsBlob.writeRef()))) {
      checkDiagnostics(diagnosticsBlob);
      throw std::runtime_error("Failed to compose program");
    }

    // 7. Link the program
    Slang::ComPtr<slang::IComponentType> linkedProgram;
    if (SLANG_FAILED(program->link(linkedProgram.writeRef(), diagnosticsBlob.writeRef()))) {
      checkDiagnostics(diagnosticsBlob);
      throw std::runtime_error("Failed to link program");
    }

    // 8. Get compiled SPIR-V code
    Slang::ComPtr<slang::IBlob> spirvCode;
    if (SLANG_FAILED(linkedProgram->getEntryPointCode(
            0,  // entry point index (we only have one in this composed program)
            0,  // target index
            spirvCode.writeRef(), diagnosticsBlob.writeRef()))) {
      checkDiagnostics(diagnosticsBlob);
      throw std::runtime_error("Failed to get entry point code");
    }


    // auto funcReflection = entryPoint->getFunctionReflection();
    // fmt::println("Compiled entry point: {}", funcReflection->getName());
    // continue;


    // 9. Build CompiledShader
    CompiledShader shader;

    // Copy SPIR-V bytes
    const u8* spirvBytes = (const u8*)spirvCode->getBufferPointer();
    size_t spirvSize = spirvCode->getBufferSize();
    shader.spirv.assign(spirvBytes, spirvBytes + spirvSize);

    // Get shader stage from SPIR-V by inspecting the execution model
    // SPIR-V execution models: 0=Vertex, 1=Fragment/Pixel, 2=GLCompute, 3=Geometry, 4=TessEval,
    // 5=TessControl The execution model is in the first instruction after the module header
    if (shader.spirv.size() >= 20) {  // SPIR-V header is 5 words (20 bytes)
      uint32_t* spirvWords = (uint32_t*)shader.spirv.data();
      uint32_t execModel = spirvWords[4] >> 16;  // ExecutionModel is in upper 16 bits of word 4
      switch (execModel) {
        case 0: shader.stage = VK_SHADER_STAGE_VERTEX_BIT; break;
        case 1: shader.stage = VK_SHADER_STAGE_FRAGMENT_BIT; break;
        case 2: shader.stage = VK_SHADER_STAGE_COMPUTE_BIT; break;
        case 3: shader.stage = VK_SHADER_STAGE_GEOMETRY_BIT; break;
        case 4: shader.stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT; break;
        case 5: shader.stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT; break;
        default: shader.stage = VK_SHADER_STAGE_ALL; break;
      }
    } else {
      shader.stage = VK_SHADER_STAGE_ALL;
    }

    // Capture entry point name and parameter count from entry point reflection
    // Note: We use the entry point object directly to get metadata
    auto funcReflection = entryPoint->getFunctionReflection();
    if (auto name = funcReflection->getName()) { shader.name = name; }
    shader.parameterCount = funcReflection->getParameterCount();

    // Store the linked program (keeps layout and reflection data alive)
    shader.linkedProgram = std::move(linkedProgram);

    results.push_back(std::move(shader));
  }

  return results;
}

// Helper to convert ParameterCategory to string
static const char* parameterCategoryToString(SlangParameterCategory category) {
  switch (category) {
    case SLANG_PARAMETER_CATEGORY_NONE: return "None";
    case SLANG_PARAMETER_CATEGORY_CONSTANT_BUFFER: return "ConstantBuffer";
    case SLANG_PARAMETER_CATEGORY_SHADER_RESOURCE: return "ShaderResource";
    case SLANG_PARAMETER_CATEGORY_UNORDERED_ACCESS: return "UnorderedAccess";
    case SLANG_PARAMETER_CATEGORY_VARYING_INPUT: return "VaryingInput";
    case SLANG_PARAMETER_CATEGORY_VARYING_OUTPUT: return "VaryingOutput";
    case SLANG_PARAMETER_CATEGORY_SAMPLER_STATE: return "SamplerState";
    case SLANG_PARAMETER_CATEGORY_UNIFORM: return "Uniform";
    case SLANG_PARAMETER_CATEGORY_PUSH_CONSTANT_BUFFER: return "PushConstantBuffer";
    default: return "Unknown";
  }
}

// Helper to convert TypeReflection::Kind to string
static const char* typeKindToString(SlangTypeKind kind) {
  switch (kind) {
    case SLANG_TYPE_KIND_NONE: return "None";
    case SLANG_TYPE_KIND_STRUCT: return "Struct";
    case SLANG_TYPE_KIND_ARRAY: return "Array";
    case SLANG_TYPE_KIND_MATRIX: return "Matrix";
    case SLANG_TYPE_KIND_VECTOR: return "Vector";
    case SLANG_TYPE_KIND_SCALAR: return "Scalar";
    case SLANG_TYPE_KIND_CONSTANT_BUFFER: return "ConstantBuffer";
    case SLANG_TYPE_KIND_RESOURCE: return "Resource";
    case SLANG_TYPE_KIND_SAMPLER_STATE: return "SamplerState";
    case SLANG_TYPE_KIND_TEXTURE_BUFFER: return "TextureBuffer";
    default: return "Unknown";
  }
}

int main(int argc, char* argv[]) {
  ren::parseFlags(argc, argv);

  // slangCompileTest();

  auto shaders = compileSlangShaders("./test.slang");
  // auto shaders = compileSlangShaders("./src/ren/res/shaders/display.slang");

  for (const auto& shader : shaders) {
    fmt::println("Compiled shader '{}' (stage {}) with {} bytes of SPIR-V, {} parameters",
                 shader.name, static_cast<uint32_t>(shader.stage), shader.spirv.size(),
                 shader.parameterCount);


    ren::ShaderReflection reflFromSpirv;
    reflFromSpirv.parseFromSpirv(shader.spirv.data(), shader.spirv.size());


    std::cout << "Reflection (from SPIR-V):" << reflFromSpirv.getRoot()->toJson().dump(2)
              << std::endl;
    std::cout << std::endl;

    // Also test reflection from Slang ProgramLayout
    if (auto layout = shader.getLayout()) {
      ren::ShaderReflection reflFromSlang;
      reflFromSlang.parseFromSlang(layout);
      std::cout << std::endl;
      std::cout << "Reflection (from Slang):" << reflFromSlang.getRoot()->toJson().dump(2)
                << std::endl;
      std::cout << std::endl;
    }
  }

  // return 0;

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
