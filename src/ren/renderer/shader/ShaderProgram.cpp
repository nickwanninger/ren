#include <ren/renderer/shader/ShaderProgram.h>
#include <ren/renderer/shader/ShaderReflection.h>
#include <ren/renderer/Swapchain.h>
#include <slang-com-ptr.h>
#include <slang.h>
#include <unistd.h>
#include <algorithm>
#include <ren/renderer/Renderer.h>
#include <ren/renderer/GlobalDescriptors.h>
#include <ren/assets/AssetManager.h>
#include <fmt/format.h>
#include <ren/misc/DeprecationLogger.h>
#include <imgui/imgui.h>
#include <ren/core/ui/EditorUI.h>
#include <ren/core/Bundle.h>
#include <ren/core/Flag.h>
#include <ren/renderer/shader/SlangFileSystem.h>


namespace ren {
  namespace {
    bool isGlobalABIBinding(u32 set, u32 binding, VkDescriptorType type) {
      return
          (set == GlobalDescriptorABI::frameSet &&
           binding == GlobalDescriptorABI::frameBinding &&
           type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) ||
          (set == GlobalDescriptorABI::heapSet &&
           binding == GlobalDescriptorABI::samplerBinding &&
           type == VK_DESCRIPTOR_TYPE_SAMPLER) ||
          (set == GlobalDescriptorABI::heapSet &&
           binding == GlobalDescriptorABI::combinedImageSamplerBinding &&
           type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) ||
          (set == GlobalDescriptorABI::heapSet &&
           binding == GlobalDescriptorABI::sampledImageBinding &&
           type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) ||
          (set == GlobalDescriptorABI::heapSet &&
           binding == GlobalDescriptorABI::storageImageBinding &&
           type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    }
  }  // namespace

  ShaderProgram::ShaderProgram(const std::string& slangPath)
      : shaderPath(slangPath) {
    this->compileFromSlangPath(slangPath);

    // Slang reflection intentionally omits its synthesized descriptor heaps.
    // Validate the emitted modules too, so the actual Vulkan ABI is enforced.
    ShaderReflection physicalReflection;
    for (auto& shader : shaders) {
      physicalReflection.parseFromSpirv(
          reinterpret_cast<const u8*>(shader->getCode().data()),
          shader->getCode().size() * sizeof(u32));
    }
    for (const auto& binding : physicalReflection.bindings) {
      if (!isGlobalABIBinding(
              binding.set, binding.index,
              binding.type.toVkDescriptorType())) {
        throw std::runtime_error(fmt::format(
            "Shader '{}' emitted descriptor set {} binding {} outside REN's global ABI",
            slangPath, binding.set, binding.index));
      }
    }
  }

  ShaderProgram::~ShaderProgram() {
    auto& vulkan = getVulkan();



    // Delete the pipeline layout.
    if (pipelineLayout != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(vulkan.device, pipelineLayout, nullptr);
      pipelineLayout = VK_NULL_HANDLE;
    }

    // Descriptor set layouts are owned by Renderer::GlobalDescriptors.
  }



  static Slang::ComPtr<slang::IGlobalSession> globalSession;

  static ren::Flag<int> kSlangOptLevel("slang-opt-level", 2, "Optimization level for Slang compiler (0=None, 1=Default, 2=Maximum)");

  // Helper to check diagnostics
  static void checkSlangDiagnostics(slang::IBlob* diagnosticsBlob) {
    if (diagnosticsBlob && diagnosticsBlob->getBufferSize() > 0) {
      const char* msg = (const char*)diagnosticsBlob->getBufferPointer();
      throw std::runtime_error(std::string("Slang compilation error: ") + msg);
    }
  }

  // Helper to convert Slang stage to Vulkan stage
  static VkShaderStageFlagBits slangStageToVulkan(SlangStage stage) {
    switch (stage) {
      case SLANG_STAGE_VERTEX:
        return VK_SHADER_STAGE_VERTEX_BIT;
      case SLANG_STAGE_FRAGMENT:
        return VK_SHADER_STAGE_FRAGMENT_BIT;
      case SLANG_STAGE_COMPUTE:
        return VK_SHADER_STAGE_COMPUTE_BIT;
      case SLANG_STAGE_GEOMETRY:
        return VK_SHADER_STAGE_GEOMETRY_BIT;
      case SLANG_STAGE_HULL:
        return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
      case SLANG_STAGE_DOMAIN:
        return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
      default:
        throw std::runtime_error("Unsupported shader stage");
    }
  }

  void ShaderProgram::compileFromSlangPath(const std::string& slangFilePath) {
    Slang::ComPtr<slang::IBlob> diagnosticsBlob;
    Slang::ComPtr<slang::IModule> module;

    // First, we need to make sure the global session exists.
    if (globalSession.get() == nullptr) {
      REN_PROFILE_SCOPE("CreateGlobalSession");
      if (SLANG_FAILED(slang::createGlobalSession(globalSession.writeRef()))) {
        throw std::runtime_error("Failed to create Slang global session");
      }
    }



    // now, create our local session.
    {
      REN_PROFILE_SCOPE("CreateLocalSession");


      slang::SessionDesc sessionDesc = {};
      slang::TargetDesc targetDesc = {};
      targetDesc.format = SLANG_SPIRV;
      targetDesc.profile = globalSession->findProfile("spirv_1_5");

      // Enable optimization
      slang::CompilerOptionEntry optimizationOption = {};
      optimizationOption.name = slang::CompilerOptionName::Optimization;
      optimizationOption.value.kind = slang::CompilerOptionValueKind::Int;
      optimizationOption.value.intValue0 = kSlangOptLevel;  // 0=None, 1=Default, 2=Maximum



      std::vector<slang::CompilerOptionEntry> compilerOptions;
      compilerOptions.push_back(optimizationOption);

      slang::CompilerOptionEntry matrixLayoutOption = {};
      matrixLayoutOption.name = slang::CompilerOptionName::MatrixLayoutColumn;
      matrixLayoutOption.value.kind = slang::CompilerOptionValueKind::Int;
      matrixLayoutOption.value.intValue0 = 1;
      compilerOptions.push_back(matrixLayoutOption);

      slang::CompilerOptionEntry bindlessSpaceOption = {};
      bindlessSpaceOption.name = slang::CompilerOptionName::BindlessSpaceIndex;
      bindlessSpaceOption.value.kind = slang::CompilerOptionValueKind::Int;
      bindlessSpaceOption.value.intValue0 = GlobalDescriptorABI::heapSet;
      compilerOptions.push_back(bindlessSpaceOption);

      targetDesc.compilerOptionEntries = compilerOptions.data();
      targetDesc.compilerOptionEntryCount = static_cast<SlangInt>(compilerOptions.size());

      sessionDesc.targets = &targetDesc;
      sessionDesc.targetCount = 1;

      static SlangFileSystem fileSystem;
      sessionDesc.fileSystem = &fileSystem;

      const char* searchPaths[] = {"shaders"};
      sessionDesc.searchPaths = searchPaths;
      sessionDesc.searchPathCount = 1;

      if (SLANG_FAILED(globalSession->createSession(sessionDesc, this->session.writeRef()))) {
        throw std::runtime_error("Failed to create Slang session");
      }
    }


    // Load the module from the filepath.
    {
      REN_PROFILE_SCOPE("LoadSlangModule");
      module = this->session->loadModule(slangFilePath.c_str(), diagnosticsBlob.writeRef());
      if (!module) {
        checkSlangDiagnostics(diagnosticsBlob);
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
      // std::array<slang::IComponentType*, 2> componentTypes = {module, entryPoint};
      allComponents.push_back(entryPoint);
    }



    // Link the modules.
    {
      Slang::ComPtr<slang::IComponentType> unlinkedProgram;
      if (SLANG_FAILED(this->session->createCompositeComponentType(allComponents.data(), allComponents.size(), unlinkedProgram.writeRef()))) {
        checkSlangDiagnostics(diagnosticsBlob);
        throw std::runtime_error("Failed to create composite program");
      }


      if (SLANG_FAILED(unlinkedProgram->link(this->program.writeRef(), diagnosticsBlob.writeRef()))) {
        checkSlangDiagnostics(diagnosticsBlob);
        throw std::runtime_error("Failed to link program");
      }
    }


    for (SlangInt32 i = 0; i < entryPointCount; ++i) {
      // 8. Get compiled SPIR-V code
      Slang::ComPtr<slang::IBlob> spirvCode;

      {
        REN_PROFILE_SCOPE("GetEntryPointCode");
        if (SLANG_FAILED(this->program->getEntryPointCode(i,  // entry point index (we only have one in this composed program)
                                                          0,  // target index
                                                          spirvCode.writeRef(), diagnosticsBlob.writeRef()))) {
          checkSlangDiagnostics(diagnosticsBlob);
          throw std::runtime_error("Failed to get entry point code");
        }


        const u8* spirvBytes = (const u8*)spirvCode->getBufferPointer();
        size_t spirvSize = spirvCode->getBufferSize();
        std::vector<u8> spirv;
        spirv.assign(spirvBytes, spirvBytes + spirvSize);


        auto* ep = this->program->getLayout()->getEntryPointByIndex(i);
        SlangStage slangStage = ep->getStage();
        VkShaderStageFlagBits vulkanStage = slangStageToVulkan(slangStage);

        const char* name = "unknown";
        if (auto epName = ep->getName()) {
          name = epName;
        }

        auto mod = make<ShaderModule>(name, spirv, vulkanStage);
        shaders.push_back(mod);
      }
    }

    this->reflection = make<ren::ShaderReflection>();
    this->reflection->parseFromSlang(this->program->getLayout());


    for (auto& r : this->reflection->bindings) {
      ShaderBinding b = {
          .name = r.path,
          .set = r.set,
          .binding = r.index,
          .count = r.count,
          .type = r.type.toVkDescriptorType(),
          .stages = VK_SHADER_STAGE_ALL,  // TODO!!!
      };
      this->bindings.push_back(b);
    }

    for (const auto& binding : bindings) {
      if (!isGlobalABIBinding(binding.set, binding.binding, binding.type)) {
        throw std::runtime_error(fmt::format(
            "Shader '{}' declares descriptor set {} binding {} outside REN's global ABI; "
            "pass per-shader data through push constants, descriptor handles, or buffer addresses",
            slangFilePath, binding.set, binding.binding));
      }
    }

    this->bakeLayouts();
  }


  void ShaderProgram::bakeLayouts() {
    auto& vulkan = getVulkan();
    auto& globals = Renderer::get().getGlobalDescriptors();
    setLayouts = {globals.frameLayout(), globals.heapLayout()};
    pushConstantRanges = {{
        .stageFlags = VK_SHADER_STAGE_ALL,
        .offset = 0,
        .size = GlobalDescriptorABI::pushConstantBytes,
    }};

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = static_cast<u32>(setLayouts.size()),
        .pSetLayouts = setLayouts.data(),
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = pushConstantRanges.data(),
    };
    VK_CHECK(vkCreatePipelineLayout(
        vulkan.device, &pipelineLayoutInfo, nullptr, &pipelineLayout));
  }
  const ShaderBinding* ShaderProgram::getBinding(const std::string_view& name) const {
    // TODO: as we grow, we need a faster lookup mechanism!
    for (const auto& binding : bindings) {
      if (binding.name == name) {
        return &binding;
      }
    }
    return nullptr;  // Not found
  }

  const ShaderBinding* ShaderProgram::getBinding(u32 set, u32 binding) const {
    for (const auto& b : bindings) {
      if (b.set == set && b.binding == binding) {
        return &b;
      }
    }
    return nullptr;
  }



  void ShaderProgram::inspect(void) {
    auto colFlags = ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_WidthStretch;
    // if (eui::ButtonGreen("Serialize to Disk", ICON_SAVE)) {
    //   this->temporarySerialize("out/shaders");
    // }
    ImGui::Text("Shader Modules:");
    static ImGuiTableFlags flags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoBordersInBody;
    if (ImGui::BeginTable("##ShaderProgramModules", 5, flags)) {
      ImGui::TableSetupColumn("Name", colFlags);
      ImGui::TableSetupColumn("Type", colFlags);
      ImGui::TableSetupColumn("SPIR-V Size", colFlags);
      ImGui::TableSetupColumn("Handle", colFlags);
      ImGui::TableSetupColumn("##", colFlags);
      ImGui::TableHeadersRow();

      for (auto& module : shaders) {
        ImGui::PushID(&module);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%s", module->getFilename().c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%d", module->getStage());

        ImGui::TableNextColumn();
        ImGui::Text("%zu bytes", module->getCode().size() * sizeof(u32));


        ImGui::TableNextColumn();
        ImGui::Text("%p", (void*)module->getHandle());

        ImGui::TableNextColumn();
        if (ImGui::Button(fmt::format("Dump##{}", module->getFilename()).c_str())) {
          // Dump SPIR-V to file
          auto dumpPath = fmt::format("{}.spv", module->getFilename());
          std::ofstream ofs(dumpPath, std::ios::binary);
          ofs.write(reinterpret_cast<const char*>(module->getCode().data()), module->getCode().size() * sizeof(u32));
          ofs.close();
          ren::println("Dumped SPIR-V to {}", dumpPath);

          // if `system` is defined, call spirv-dis on it
          system(fmt::format("spirv-dis {}", dumpPath).c_str());
          system(fmt::format("spirv-reflect {}", dumpPath).c_str());
          unlink(dumpPath.c_str());
          ren::ShaderReflection refl;
          refl.parseFromSpirv(reinterpret_cast<const u8*>(module->getCode().data()), module->getCode().size() * sizeof(u32));
          // ren::println("Reflection:\n{}", refl.getRoot()->toJson().dump(2));

          for (const auto& b : refl.bindings) {
            ren::println("Binding: set {} binding {} name {}", b.set, b.index, b.path);
          }
        }
        ImGui::PopID();
      }
      ImGui::EndTable();
    }


    if (reflection) {
      ImGui::SeparatorText("Shader Reflection");
      reflection->inspect();
    }


    if (ImGui::BeginTable("##ShaderProgramBindings", 5, flags)) {
      ImGui::TableSetupColumn("Set", colFlags);
      ImGui::TableSetupColumn("Binding", colFlags);
      ImGui::TableSetupColumn("Type", colFlags);
      ImGui::TableSetupColumn("Count", colFlags);
      ImGui::TableSetupColumn("Stages", colFlags);
      ImGui::TableHeadersRow();

      for (const auto& binding : bindings) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%u", binding.set);
        ImGui::TableNextColumn();
        ImGui::Text("%u", binding.binding);
        ImGui::TableNextColumn();
        ImGui::Text("%s", ren::VulkanInstance::stringifyEnum(binding.type));
        ImGui::TableNextColumn();
        ImGui::Text("%u", binding.count);
        ImGui::TableNextColumn();
        ImGui::Text("0x%X", binding.stages);
      }
      ImGui::EndTable();
    }


    ImGui::Separator();
    ImGui::Text("Descriptor Set Layouts:");
    if (ImGui::BeginTable("##ShaderProgramLayouts", 2, flags)) {
      ImGui::TableSetupColumn("Set", colFlags);
      ImGui::TableSetupColumn("Handle", colFlags);
      ImGui::TableHeadersRow();

      for (size_t i = 0; i < setLayouts.size(); ++i) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%zu", i);
        ImGui::TableNextColumn();
        ImGui::Text("%p", (void*)setLayouts[i]);
      }
      ImGui::EndTable();
    }


    ImGui::Separator();
    ImGui::Text("Push Constant Ranges:");
    if (ImGui::BeginTable("##PushConstantRanges", 3, flags)) {
      ImGui::TableSetupColumn("Offset", colFlags);
      ImGui::TableSetupColumn("Size", colFlags);
      ImGui::TableSetupColumn("Stages", colFlags);
      ImGui::TableHeadersRow();

      for (const auto& range : pushConstantRanges) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%u", range.offset);
        ImGui::TableNextColumn();
        ImGui::Text("%u", range.size);
        ImGui::TableNextColumn();
        ImGui::Text("0x%X", range.stageFlags);
      }
      ImGui::EndTable();
    }


    ImGui::Separator();
  }


  void ShaderProgram::temporarySerialize(const std::string_view& outDir) {
    // Ensure output directory exists
    std::filesystem::create_directories(outDir);

    json j;
    j["reflection"] = reflection->toJson();


    ren::BundleBuilder bundle;

    // write the shader programs to disk as spirv.
    auto shadersJson = json::array();
    for (auto& shader : shaders) {
      json js;
      js["name"] = shader->getFilename();
      js["stage"] = shader->getStage();

      auto& code = shader->getCode();
      auto blobIndex = bundle.attachBlob(code);
      js["blob"] = blobIndex;

      shadersJson.push_back(js);
    }
    bundle.setKey("shaders", shadersJson);

    auto outPath = std::filesystem::path(outDir) / "shader_program.bundle";
    bundle.write(outPath.string());

    // // Serialize to JSON
    // auto json = this->toJson();

    // // Write to file
    // auto outPath = std::filesystem::path(outDir) / "shader_program.json";
    // std::ofstream ofs(outPath);
    // ofs << json.dump(2);
    // ofs.close();

    // ren::println("Serialized ShaderProgram to {}", outPath.string());
  }



}  // namespace ren
