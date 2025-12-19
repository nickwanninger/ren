#include <ren/renderer/SlangCompiler.h>
#include <ren/core/Instrumentation.h>

#include "slang.h"
#include "slang-com-ptr.h"
#include "slang-com-helper.h"
#include <ren/core/Flag.h>

static ren::Flag<int> kSlangOptLevel(
    "slang-opt-level", 2, "Optimization level for Slang compiler (0=None, 1=Default, 2=Maximum)");

namespace ren {

  namespace detail {
    // Helper to convert Slang stage to Vulkan stage
    static VkShaderStageFlagBits slangStageToVulkan(SlangStage stage) {
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
    static void checkDiagnostics(slang::IBlob* diagnosticsBlob) {
      if (diagnosticsBlob && diagnosticsBlob->getBufferSize() > 0) {
        const char* msg = (const char*)diagnosticsBlob->getBufferPointer();
        throw std::runtime_error(std::string("Slang compilation error: ") + msg);
      }
    }


    static Slang::ComPtr<slang::IGlobalSession> globalSession;
  }  // namespace detail

  SlangCompilationResult compileSlangShaders(const char* slangFilePath) {
    REN_PROFILE_SCOPE("SlangCompile");



    SlangCompilationResult result;

    result.reflection = make<ShaderReflection>();

    Slang::ComPtr<slang::ISession> session;  // Local Session.

    Slang::ComPtr<slang::IBlob> diagnosticsBlob;
    Slang::ComPtr<slang::IModule> module;

    // Make sure the global session is created.
    if (detail::globalSession.get() == nullptr) {
      REN_PROFILE_SCOPE("CreateGlobalSession");
      if (SLANG_FAILED(slang::createGlobalSession(detail::globalSession.writeRef()))) {
        throw std::runtime_error("Failed to create Slang global session");
      }
    }




    {
      REN_PROFILE_SCOPE("CreateSession");
      // 2. Configure session for SPIR-V target
      slang::SessionDesc sessionDesc = {};
      slang::TargetDesc targetDesc = {};
      targetDesc.format = SLANG_SPIRV;
      targetDesc.profile = detail::globalSession->findProfile("spirv_1_5");

      // Enable Vulkan reflection to get parameter information
      slang::CompilerOptionEntry reflectionOption = {};
      reflectionOption.name = slang::CompilerOptionName::VulkanEmitReflection;
      reflectionOption.value.kind = slang::CompilerOptionValueKind::Int;
      reflectionOption.value.intValue0 = 1;

      // Enable optimization
      slang::CompilerOptionEntry optimizationOption = {};
      optimizationOption.name = slang::CompilerOptionName::Optimization;
      optimizationOption.value.kind = slang::CompilerOptionValueKind::Int;
      optimizationOption.value.intValue0 = kSlangOptLevel;  // 0=None, 1=Default, 2=Maximum

      std::vector<slang::CompilerOptionEntry> compilerOptions;
      compilerOptions.push_back(reflectionOption);
      compilerOptions.push_back(optimizationOption);

      targetDesc.compilerOptionEntries = compilerOptions.data();
      targetDesc.compilerOptionEntryCount = static_cast<SlangInt>(compilerOptions.size());

      sessionDesc.targets = &targetDesc;
      sessionDesc.targetCount = 1;

      // TODO: virtual filesystem!

      if (SLANG_FAILED(detail::globalSession->createSession(sessionDesc, session.writeRef()))) {
        throw std::runtime_error("Failed to create Slang session");
      }
    }


    {
      REN_PROFILE_SCOPE("LoadSlangModule");
      module = session->loadModule(slangFilePath, diagnosticsBlob.writeRef());
      if (!module) {
        detail::checkDiagnostics(diagnosticsBlob);
        throw std::runtime_error("Failed to load Slang module");
      }
    }



    // Get the entry point count.
    SlangInt32 entryPointCount = module->getDefinedEntryPointCount();
    if (entryPointCount == 0) {
      throw std::runtime_error("No entry points found in shader source");
    }



    // 5. Compile each entry point
    for (SlangInt32 i = 0; i < entryPointCount; ++i) {
      REN_PROFILE_SCOPE("CompileSlangEntryPoint");
      Slang::ComPtr<slang::IEntryPoint> entryPoint;

      {
        REN_PROFILE_SCOPE("GetDefinedEntryPoint");
        if (SLANG_FAILED(module->getDefinedEntryPoint(i, entryPoint.writeRef()))) {
          throw std::runtime_error("Failed to get entry point");
        }
      }

      // 6. Compose the program (module + entry point)
      std::array<slang::IComponentType*, 2> componentTypes = {module, entryPoint};

      Slang::ComPtr<slang::IComponentType> program;
      {
        REN_PROFILE_SCOPE("CreateCompositeComponentType");
        if (SLANG_FAILED(session->createCompositeComponentType(
                componentTypes.data(), componentTypes.size(), program.writeRef(),
                diagnosticsBlob.writeRef()))) {
          detail::checkDiagnostics(diagnosticsBlob);
          throw std::runtime_error("Failed to compose program");
        }
      }

      // 7. Link the program
      Slang::ComPtr<slang::IComponentType> linkedProgram;
      {
        REN_PROFILE_SCOPE("LinkProgram");
        if (SLANG_FAILED(program->link(linkedProgram.writeRef(), diagnosticsBlob.writeRef()))) {
          detail::checkDiagnostics(diagnosticsBlob);
          throw std::runtime_error("Failed to link program");
        }
      }

      // 8. Get compiled SPIR-V code
      Slang::ComPtr<slang::IBlob> spirvCode;

      {
        REN_PROFILE_SCOPE("GetEntryPointCode");
        if (SLANG_FAILED(linkedProgram->getEntryPointCode(
                0,  // entry point index (we only have one in this composed program)
                0,  // target index
                spirvCode.writeRef(), diagnosticsBlob.writeRef()))) {
          detail::checkDiagnostics(diagnosticsBlob);
          throw std::runtime_error("Failed to get entry point code");
        }
      }






      // 9. Build CompiledShader
      SlangCompilationResult::Module shader;

      // Copy SPIR-V bytes
      const u8* spirvBytes = (const u8*)spirvCode->getBufferPointer();
      size_t spirvSize = spirvCode->getBufferSize();
      shader.spirv.assign(spirvBytes, spirvBytes + spirvSize);


      SlangStage slangStage = linkedProgram->getLayout()->getEntryPointByIndex(0)->getStage();
      shader.stage = detail::slangStageToVulkan(slangStage);


      // Grab the entry point name
      auto funcReflection = entryPoint->getFunctionReflection();
      if (auto name = funcReflection->getName()) { shader.name = name; }

      // Update the reflection info with this entry point's data.
      auto layout = linkedProgram->getLayout();
      if (layout == nullptr) {
        throw std::runtime_error("No ProgramLayout available for reflection");
      }
      result.reflection->parseFromSlang(layout);

      result.modules.push_back(std::move(shader));
    }



    return result;
  }
}  // namespace ren