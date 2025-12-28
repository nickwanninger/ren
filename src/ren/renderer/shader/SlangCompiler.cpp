#include <ren/renderer/shader/SlangCompiler.h>
#include <ren/core/Instrumentation.h>
#include <ren/renderer/shader/ShaderCursor.h>

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
    static Slang::ComPtr<slang::ISession> session;  // Local Session.
  }  // namespace detail

  SlangCompilationResult compileSlangShaders(const char* slangFilePath) {
    REN_PROFILE_SCOPE("SlangCompile");



    SlangCompilationResult result;

    result.reflection = make<ShaderReflection>();


    Slang::ComPtr<slang::IBlob> diagnosticsBlob;
    Slang::ComPtr<slang::IModule> module;

    // Make sure the global session is created.
    if (detail::globalSession.get() == nullptr) {
      REN_PROFILE_SCOPE("CreateGlobalSession");
      if (SLANG_FAILED(slang::createGlobalSession(detail::globalSession.writeRef()))) {
        throw std::runtime_error("Failed to create Slang global session");
      }
    }




    if (detail::session.get() == nullptr) {
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

      if (SLANG_FAILED(
              detail::globalSession->createSession(sessionDesc, detail::session.writeRef()))) {
        throw std::runtime_error("Failed to create Slang session");
      }
    }


    {
      REN_PROFILE_SCOPE("LoadSlangModule");
      module = detail::session->loadModule(slangFilePath, diagnosticsBlob.writeRef());
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



    std::vector<slang::IComponentType*> allComponents;
    allComponents.push_back(module);

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
      allComponents.push_back(entryPoint);

      Slang::ComPtr<slang::IComponentType> program;
      {
        REN_PROFILE_SCOPE("CreateCompositeComponentType");
        if (SLANG_FAILED(detail::session->createCompositeComponentType(
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
      // result.reflection->parseFromSlang(layout);

      result.modules.push_back(std::move(shader));
    }



    // now that we have all the elements from the entrypoints and the module, lets make a program
    // for later use.
    Slang::ComPtr<slang::IComponentType> program;
    if (SLANG_FAILED(detail::session->createCompositeComponentType(
            allComponents.data(), allComponents.size(), program.writeRef()))) {
      detail::checkDiagnostics(diagnosticsBlob);
      throw std::runtime_error("Failed to create composite program");
    }


    if (SLANG_FAILED(program->link(&result.program, diagnosticsBlob.writeRef()))) {
      detail::checkDiagnostics(diagnosticsBlob);
      throw std::runtime_error("Failed to link program");
    }




    result.reflection->parseFromSlang(program->getLayout());


    {
      Slang::ComPtr<slang::IBlob> jsonBlob;
      program->getLayout()->toJson(jsonBlob.writeRef());

      json j = json::parse(
          std::string_view((const char*)jsonBlob->getBufferPointer(), jsonBlob->getBufferSize()));
      FILE* f = fopen("slang_program_layout.json", "w");
      auto formatted = j.dump(2);
      fwrite(formatted.data(), 1, formatted.size(), f);
      fclose(f);
    }


    {
      FILE* f = fopen("shader_reflection.json", "w");
      auto j = result.reflection->getRoot()->toJson().dump(2);
      fwrite(j.data(), 1, j.size(), f);
      fclose(f);
    }


    return result;
  }



  void inspectSlangBindingRanges(slang::TypeLayoutReflection* programTypeLayout,
                                 slang::TypeLayoutReflection* typeLayout) {
    auto bindingRangeCount = typeLayout->getBindingRangeCount();

    if (ImGui::BeginTable("Binding Ranges", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
      ImGui::TableSetupColumn("Index");
      ImGui::TableSetupColumn("SetIndex");
      ImGui::TableSetupColumn("FirstDescIdx");
      ImGui::TableSetupColumn("DescRangeCount");
      ImGui::TableSetupColumn("set.binding");
      ImGui::TableSetupColumn("Leaf Variable");
      ImGui::TableHeadersRow();


      for (u32 i = 0; i < bindingRangeCount; ++i) {
        auto leafTypeLayout = typeLayout->getBindingRangeLeafTypeLayout(i);
        auto leafVariable = typeLayout->getBindingRangeLeafVariable(i);
        // auto imageFormat = typeLayout->getBindingRangeImageFormat(i);
        auto logicalSetIndex = typeLayout->getBindingRangeDescriptorSetIndex(i);
        auto firstDescriptorIndex = typeLayout->getBindingRangeFirstDescriptorRangeIndex(i);
        auto descriptorRangeCount = typeLayout->getBindingRangeDescriptorRangeCount(i);

        u32 vulkanSetIndex = programTypeLayout->getDescriptorSetSpaceOffset(logicalSetIndex);

        // u32 set = typeLayout->getDescriptorSetSpaceOffset(logicalSetIndex);

        u32 binding = typeLayout->getDescriptorSetDescriptorRangeIndexOffset(
            logicalSetIndex, typeLayout->getBindingRangeFirstDescriptorRangeIndex(i));

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%u", (u32)i);
        ImGui::TableNextColumn();
        ImGui::Text("%u", (u32)logicalSetIndex);
        ImGui::TableNextColumn();
        ImGui::Text("%u", (u32)firstDescriptorIndex);
        ImGui::TableNextColumn();
        ImGui::Text("%u", (u32)descriptorRangeCount);
        ImGui::TableNextColumn();
        ImGui::Text("%d.%d", (int)vulkanSetIndex, (int)binding);
        ImGui::TableNextColumn();
        ImGui::Text("%s", leafVariable ? leafVariable->getName() : "<unnamed>");
      }

      ImGui::EndTable();
    }

    // u32 setCount = typeLayout->getDescriptorSetCount();
    // ImGui::Text("Descriptor set Count: %u", typeLayout->getBindingRangeCount());
  }



  static ImGuiTreeNodeFlags tree_node_flags_base = ImGuiTreeNodeFlags_SpanAllColumns |
                                                   ImGuiTreeNodeFlags_DefaultOpen |
                                                   ImGuiTreeNodeFlags_DrawLinesFull;

  static void inspectSlangRecursive(const slang::TypeLayoutReflection* typeLayout, int depth) {
    ImGuiTreeNodeFlags node_flags = tree_node_flags_base;
    bool isLeaf = false;
  }


  void inspectSlangComponent(slang::IComponentType* ct) {
    // fmt::println("Inspecting Slang Component Type @ {}", (void*)ct);
    auto* programLayout = ct->getLayout();

    ImGui::PushID((void*)ct);


    ImGui::SeparatorText("Slang Component Inspection");

    auto globalParams = programLayout->getGlobalParamsVarLayout();


    if (!globalParams) {
      ImGui::Text("No global parameters");
      return;
    }


    const float TEXT_BASE_WIDTH = ImGui::CalcTextSize("A").x;
    float locWidth = TEXT_BASE_WIDTH * 3.5f;
    ImGuiTableColumnFlags locFlags = ImGuiTableColumnFlags_WidthFixed |
                                     ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_NoResize;

    static ImGuiTableFlags flags =
        ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_RowBg;


    if (ImGui::BeginTable("##SlangComponent", 3, flags)) {
      ImGui::TableSetupColumn("Name");
      ImGui::TableSetupColumn("CAT", locFlags);
      ImGui::TableSetupColumn("VAL", locFlags);
      ImGui::TableHeadersRow();


      auto* ty = globalParams->getTypeLayout();
      for (u32 i = 0; i < ty->getFieldCount(); ++i) {
        auto varLayout = ty->getFieldByIndex(i);
        auto typeLayout = varLayout->getTypeLayout();

        // u32 set = varLayout->getBindingSpace(SLANG_PARAAMETER_CATEGORY_REGISTER_SPACE);
        // u32 ind = varLayout->getBindingIndex();

        u32 numCats = varLayout->getCategoryCount();
        for (u32 cat = 0; cat < numCats; ++cat) {
          auto catKind = varLayout->getCategoryByIndex(cat);
          u32 catValue = varLayout->getBindingSpace(catKind);

          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::Text("%s (%u/%u) ", varLayout->getName(), cat + 1, numCats);
          ImGui::TableNextColumn();
          ImGui::Text("%u", (u32)catKind);
          ImGui::TableNextColumn();
          ImGui::Text("%u", (u32)catValue);
        }
      }

      ImGui::EndTable();
    }
    ImGui::PopID();


#if 0

    if (globalParams) {
      auto typeLayout = globalParams->getTypeLayout();
      inspectSlangBindingRanges(typeLayout, typeLayout);

      typeLayout->getDescriptorSetSpaceOffset(0);

      u32 setCount = typeLayout->getDescriptorSetCount();
      for (u32 setIndex = 0; setIndex < setCount; ++setIndex) {
        auto vkSet = typeLayout->getDescriptorSetSpaceOffset(setIndex);
        ImGui::Text(" Set logical=%u, vk=%u", (u32)setIndex, (u32)vkSet);
      }

      ImGui::Separator();

      if (typeLayout) {
        // Traverse global scope (usually a struct containing all globals)
        u32 fieldCount = typeLayout->getFieldCount();
        ImGui::Text("Global Parameters (%u):", fieldCount);
        ShaderCursor cursor(typeLayout);
        cursor.inspect(typeLayout);
      } else {
        ImGui::Text("No global type layout");
      }
    } else {
      ImGui::Text("No global parameters");
    }
#endif



    // SlangUInt entryPointCount = pl->getEntryPointCount();
    // for (SlangUInt i = 0; i < entryPointCount; ++i) {
    //   auto epReflection = pl->getEntryPointByIndex(i);
    //   auto epTypeLayout = epReflection->getTypeLayout();
    //   ImGui::Text("Entry Point %u Parameters:", i);
    //   Cursor epCursor(epTypeLayout);
    //   epCursor.inspect();
    // }
  }
}  // namespace ren