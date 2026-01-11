#include <ren/renderer/shader/ShaderProgram.h>
#include <ren/renderer/shader/ShaderReflection.h>
#include <ren/renderer/Swapchain.h>
#include <slang-com-ptr.h>
#include <slang.h>
#include <unistd.h>
#include <algorithm>
#include <ren/assets/AssetManager.h>
#include <fmt/format.h>
#include <ren/misc/DeprecationLogger.h>
#include <imgui/imgui.h>
#include <ren/core/ui/EditorUI.h>
#include <ren/core/Bundle.h>
#include <ren/core/Flag.h>


namespace ren {

  ShaderProgram::ShaderProgram(const std::string& slangPath) {
    ren::println("-- Slang --");
    this->compileFromSlangPath(slangPath);

    // Reflect the spirv to compare with the slang reflection.
    // ren::println("-- Spirv --");
    // ShaderReflection spirvReflection;
    // for (auto& shader : shaders) {
    //   spirvReflection.parseFromSpirv(reinterpret_cast<const u8*>(shader->getCode().data()), shader->getCode().size() * sizeof(u32));
    // }

    // ren::println("Slang Reflection: {}", this->reflection->toJson().dump(2));
  }

  ShaderProgram::ShaderProgram(const std::string& vertexPath, const std::string& fragmentPath)
      : vertexShaderPath(vertexPath)
      , fragmentShaderPath(fragmentPath) {
    REN_DEPRECATION_WARNING();
    shaders.push_back(ren::getAsset<VertexShader>(vertexPath));
    shaders.push_back(ren::getAsset<FragmentShader>(fragmentPath));


    reflectShaders();
    bakeLayouts();
  }


  ShaderProgram::~ShaderProgram() {
    auto& vulkan = getVulkan();



    // Delete the pipeline layout.
    if (pipelineLayout != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(vulkan.device, pipelineLayout, nullptr);
      pipelineLayout = VK_NULL_HANDLE;
    }


    // delete all the set layouts
    for (auto& setLayout : setLayouts) {
      if (setLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkan.device, setLayout, nullptr);
        setLayout = VK_NULL_HANDLE;
      }
    }
  }



  static Slang::ComPtr<slang::IGlobalSession> globalSession;

  static ren::Flag<int> kSlangOptLevel("slang-opt-level", 0, "Optimization level for Slang compiler (0=None, 1=Default, 2=Maximum)");

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

      {
        slang::CompilerOptionEntry opt = {};
        opt.name = slang::CompilerOptionName::VulkanEmitReflection;
        opt.value.kind = slang::CompilerOptionValueKind::Int;
        opt.value.intValue0 = 1;
        compilerOptions.push_back(opt);
      }


      {
        slang::CompilerOptionEntry opt = {};
        opt.name = slang::CompilerOptionName::MatrixLayoutColumn;
        opt.value.kind = slang::CompilerOptionValueKind::Int;
        opt.value.intValue0 = 1;
        compilerOptions.push_back(opt);
      }


      targetDesc.compilerOptionEntries = compilerOptions.data();
      targetDesc.compilerOptionEntryCount = static_cast<SlangInt>(compilerOptions.size());

      sessionDesc.targets = &targetDesc;
      sessionDesc.targetCount = 1;

      // TODO: viratual filesystems from ren::AssetManager!

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


    for (const auto& node : reflection->getRoot()->members) {
      if (node->type.type != ShaderReflection::Type::PushConstant) {
        continue;
      }

      auto pcLoc = node->location;
      if (!pcLoc.byteOffset || !pcLoc.byteSize) {
        ren::errln("Push constant block missing byte offset/size, skipping...");
        continue;
      }
      VkPushConstantRange range{};
      range.offset = *pcLoc.byteOffset;
      range.size = *pcLoc.byteSize;
      range.stageFlags = VK_SHADER_STAGE_ALL;
      range.stageFlags = VK_SHADER_STAGE_ALL;  // TODO: get from reflection somehow?
      pushConstantRanges.push_back(range);
    }

    for (auto& r : this->reflection->bindings) {
      ShaderBinding b = {
          .name = r.path,
          .set = r.set,
          .binding = r.index,
          .count = r.count,
          .type = ShaderReflection::BindingType::toVkDescriptorType(r.type.type),
          .stages = VK_SHADER_STAGE_ALL,  // TODO!!!
      };
      this->bindings.push_back(b);
    }

    this->bakeLayouts();
  }


  void ShaderProgram::reflectShaders() {
    // TODO: use ren::ShaderRefleciton
    for (auto& shader : shaders) {
      reflectShader(shader->getCode(), shader->getStage());
    }

    // Merge and deduplicate bindings
    // mergeDescriptorBindings();
  }


  void ShaderProgram::reflectShader(const std::vector<u32>& spirv, VkShaderStageFlagBits stage) {
    SpvReflectShaderModule module;
    SpvReflectResult result = spvReflectCreateShaderModule(spirv.size() * sizeof(u32), (u32*)spirv.data(), &module);

    if (result != SPV_REFLECT_RESULT_SUCCESS) {
      throw std::runtime_error("Failed to create SPIRV reflection module");
    }

    // Get descriptor bindings
    uint32_t bindingCount = 0;
    result = spvReflectEnumerateDescriptorBindings(&module, &bindingCount, nullptr);
    if (result != SPV_REFLECT_RESULT_SUCCESS) {
      spvReflectDestroyShaderModule(&module);
      throw std::runtime_error("Failed to enumerate descriptor bindings");
    }

    std::vector<SpvReflectDescriptorBinding*> reflectionBindings(bindingCount);
    result = spvReflectEnumerateDescriptorBindings(&module, &bindingCount, reflectionBindings.data());
    if (result != SPV_REFLECT_RESULT_SUCCESS) {
      spvReflectDestroyShaderModule(&module);
      throw std::runtime_error("Failed to get descriptor reflectionBindings");
    }

    for (const auto* binding : reflectionBindings) {
      ShaderBinding desc;
      desc.set = binding->set;
      desc.binding = binding->binding;
      desc.type = static_cast<VkDescriptorType>(binding->descriptor_type);
      // If reflection reports 0 (runtime array), use 1 for layout creation; actual arraying handled
      // by user
      desc.count = binding->count == 0 ? 16384u : binding->count;
      desc.stages = stage;
      // Some compilers may emit null/empty names; synthesize a stable name in that case
      if (binding->name && binding->name[0] != '\0') {
        desc.name = binding->name;
      } else {
        desc.name = fmt::format("set{}_binding{}", desc.set, desc.binding);
      }

      bindings.push_back(desc);
    }

    // Generate push constant ranges
    for (uint32_t i = 0; i < module.push_constant_block_count; ++i) {
      const SpvReflectBlockVariable* pushConstantBlock = &module.push_constant_blocks[i];
      VkPushConstantRange range{};
      range.offset = pushConstantBlock->offset;
      range.size = pushConstantBlock->size;
      range.stageFlags = stage;
      pushConstantRanges.push_back(range);
    }

    spvReflectDestroyShaderModule(&module);
  }

  void ShaderProgram::mergeDescriptorBindings() {
    // Merge by (set,binding), combine stages, validate consistency
    struct Key {
      u32 set;
      u32 binding;
      bool operator==(const Key& o) const { return set == o.set && binding == o.binding; }
    };
    struct KeyHash {
      size_t operator()(const Key& k) const { return (size_t(k.set) << 16) ^ k.binding; }
    };

    std::unordered_map<Key, ShaderBinding, KeyHash> mergedMap;
    for (const auto& b : bindings) {
      Key k{b.set, b.binding};
      auto it = mergedMap.find(k);
      if (it == mergedMap.end()) {
        mergedMap.emplace(k, b);
      } else {
        auto& m = it->second;
        // Validate descriptor type and count are consistent across stages
        if (m.type != b.type) {
          throw std::runtime_error(
              fmt::format("Descriptor type mismatch for set {} binding {} across stages ({} vs {})", m.set, m.binding, (int)m.type, (int)b.type));
        }
        if (m.count != b.count) {
          // Take the max to be conservative
          m.count = std::max(m.count, b.count);
        }
        // Merge stage flags
        m.stages |= b.stages;
        // Prefer a non-empty, longer name if they differ
        if (m.name != b.name) {
          if (m.name.empty() || b.name.size() > m.name.size()) {
            m.name = b.name;
          }
        }
      }
    }

    // Rebuild sorted list for stable ordering (by set then binding); allow sparse sets
    std::vector<ShaderBinding> out;
    out.reserve(mergedMap.size());
    for (auto& [k, v] : mergedMap) {
      out.push_back(v);
    }
    std::sort(out.begin(), out.end(), [](const ShaderBinding& a, const ShaderBinding& b) {
      if (a.set != b.set) {
        return a.set < b.set;
      }
      return a.binding < b.binding;
    });
    bindings = std::move(out);
  }




  void ShaderProgram::bakeLayouts() {
    auto& vulkan = getVulkan();


    // using our bindings, we can create a pipeline layout.

    // Group bindings by set number
    std::map<u32, std::vector<VkDescriptorSetLayoutBinding>> setBindings;


    u32 maxSet = 0;

    for (const auto& binding : bindings) {
      VkDescriptorSetLayoutBinding layoutBinding{};
      layoutBinding.binding = binding.binding;
      layoutBinding.descriptorType = binding.type;
      layoutBinding.descriptorCount = binding.count;
      layoutBinding.stageFlags = binding.stages;
      layoutBinding.pImmutableSamplers = nullptr;

      // ren::println("Binding: set {} binding {} type={} count={} stages=0x{:X}", binding.set, binding.binding, static_cast<int>(binding.type),
      //              binding.count, binding.stages);

      if (binding.set > maxSet) {
        maxSet = binding.set;
      }

      setBindings[binding.set].push_back(layoutBinding);
    }

    // Create descriptor set layouts
    setLayouts.clear();

    if (!setBindings.empty()) {
      // Size to max set index + 1 so sparse sets are indexed directly; holes remain VK_NULL_HANDLE
      setLayouts.assign(maxSet + 1, VK_NULL_HANDLE);
      for (const auto& [setIndex, bindings] : setBindings) {
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.flags = 0;  // VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        layoutInfo.bindingCount = static_cast<u32>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        vkCreateDescriptorSetLayout(vulkan.device, &layoutInfo, nullptr, &setLayouts[setIndex]);
      }
    }


    // Create pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<u32>(setLayouts.size());
    pipelineLayoutInfo.pSetLayouts = setLayouts.empty() ? nullptr : setLayouts.data();

    if (pushConstantRanges.empty()) {
      pipelineLayoutInfo.pushConstantRangeCount = 0;
      pipelineLayoutInfo.pPushConstantRanges = nullptr;
    } else {
      pipelineLayoutInfo.pushConstantRangeCount = static_cast<u32>(pushConstantRanges.size());
      pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.data();
    }
    vkCreatePipelineLayout(vulkan.device, &pipelineLayoutInfo, nullptr, &this->pipelineLayout);
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
    if (eui::ButtonGreen("Serialize to Disk", ICON_SAVE)) {
      this->temporarySerialize("out/shaders");
    }
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
          auto cmd = fmt::format("spirv-dis {}", dumpPath);
          system(cmd.c_str());
          unlink(dumpPath.c_str());
          ren::ShaderReflection refl;
          refl.parseFromSpirv(reinterpret_cast<const u8*>(module->getCode().data()), module->getCode().size() * sizeof(u32));
          // ren::println("Reflection:\n{}", refl.getRoot()->toJson().dump(2));

          for (const auto& b : refl.bindings) {
            ren::println("Binding: set {} binding {} name {}", b.set, b.index, b.path);
          }
        }
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
