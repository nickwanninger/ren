#include <ren/renderer/shader/ShaderProgram.h>
#include <ren/renderer/shader/ShaderReflection.h>
#include <ren/renderer/shader/SlangCompiler.h>
#include <ren/renderer/Swapchain.h>
#include <algorithm>
#include <ren/assets/AssetManager.h>
#include <fmt/format.h>
#include <ren/misc/DeprecationLogger.h>
#include <imgui/imgui.h>




namespace ren {

  ShaderProgram::ShaderProgram(const std::string& slangPath) {
    auto result = ren::compileSlangShaders(slangPath.c_str());

    this->slangProgram = std::move(result.program);
    this->reflection = std::move(result.reflection);

    // slangProgram->addRef(); // The hell is going on with this com protocol??




    auto vkDevice = getVulkan().device;
    auto globalParamsLayout =
        slangProgram->getLayout()->getGlobalParamsVarLayout()->getTypeLayout();

    fmt::println("Compiled Slang shader for {} @ {}", (void*)this, (void*)slangProgram.get());

    for (const auto& module : result.modules) {
      // Make a shader module from the SPIR-V
      auto mod = make<ShaderModule>(module.name, module.spirv, module.stage);
      shaders.push_back(mod);
    }

    bakeLayouts();
  }

  ShaderProgram::ShaderProgram(const std::string& vertexPath, const std::string& fragmentPath)
      : vertexShaderPath(vertexPath)
      , fragmentShaderPath(fragmentPath) {
    REN_DEPRECATION_WARNING();
    shaders.push_back(ren::getAsset<VertexShader>(vertexPath));
    shaders.push_back(ren::getAsset<FragmentShader>(fragmentPath));


    slangProgram = nullptr;
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


  void ShaderProgram::reflectShaders() {
    this->reflection = make<ren::ShaderReflection>();
    // TODO: use ren::ShaderRefleciton
    for (auto& shader : shaders) {
      reflectShader(shader->getCode(), shader->getStage());
      reflection->parseFromSpirv(reinterpret_cast<const u8*>(shader->getCode().data()),
                                 shader->getCode().size() * sizeof(u32));
    }

    // Merge and deduplicate bindings
    // mergeDescriptorBindings();
  }


  void ShaderProgram::reflectShader(const std::vector<u32>& spirv, VkShaderStageFlagBits stage) {
    SpvReflectShaderModule module;
    SpvReflectResult result =
        spvReflectCreateShaderModule(spirv.size() * sizeof(u32), (u32*)spirv.data(), &module);

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
    result =
        spvReflectEnumerateDescriptorBindings(&module, &bindingCount, reflectionBindings.data());
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
              fmt::format("Descriptor type mismatch for set {} binding {} across stages ({} vs {})",
                          m.set, m.binding, (int)m.type, (int)b.type));
        }
        if (m.count != b.count) {
          // Take the max to be conservative
          m.count = std::max(m.count, b.count);
        }
        // Merge stage flags
        m.stages |= b.stages;
        // Prefer a non-empty, longer name if they differ
        if (m.name != b.name) {
          if (m.name.empty() || b.name.size() > m.name.size()) m.name = b.name;
        }
      }
    }

    // Rebuild sorted list for stable ordering (by set then binding); allow sparse sets
    std::vector<ShaderBinding> out;
    out.reserve(mergedMap.size());
    for (auto& [k, v] : mergedMap)
      out.push_back(v);
    std::sort(out.begin(), out.end(), [](const ShaderBinding& a, const ShaderBinding& b) {
      if (a.set != b.set) return a.set < b.set;
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

      if (binding.set > maxSet) maxSet = binding.set;

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



    // TODO: also parse this!
    // ---- Push Constants ---- //
    VkPushConstantRange pushConstants{};
    // this push constant range starts at the beginning
    pushConstants.offset = 0;
    // this push constant range takes up the size of a MeshPushConstants struct
    pushConstants.size = sizeof(ren::MeshPushConstants);
    // this push constant range is accessible only in the vertex shader
    pushConstants.stageFlags = VK_SHADER_STAGE_ALL;

    // Create pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<u32>(setLayouts.size());
    pipelineLayoutInfo.pSetLayouts = setLayouts.empty() ? nullptr : setLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstants;
    vkCreatePipelineLayout(vulkan.device, &pipelineLayoutInfo, nullptr, &this->pipelineLayout);
  }

  const ShaderBinding* ShaderProgram::getBinding(const std::string_view& name) const {
    // TODO: as we grow, we need a faster lookup mechanism!
    for (const auto& binding : bindings) {
      if (binding.name == name) { return &binding; }
    }
    return nullptr;  // Not found
  }

  const ShaderBinding* ShaderProgram::getBinding(u32 set, u32 binding) const {
    for (const auto& b : bindings) {
      if (b.set == set && b.binding == binding) return &b;
    }
    return nullptr;
  }



  void ShaderProgram::inspect(void) {
    ImGui::Text("Shader Modules:");
    static ImGuiTableFlags flags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH |
                                   ImGuiTableFlags_RowBg | ImGuiTableFlags_NoBordersInBody;
    if (ImGui::BeginTable("##ShaderProgramModules", 5, flags)) {
      auto colFlags = ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_WidthStretch;
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
          ofs.write(reinterpret_cast<const char*>(module->getCode().data()),
                    module->getCode().size() * sizeof(u32));
          ofs.close();
          ren::println("Dumped SPIR-V to {}", dumpPath);

          // if `system` is defined, call spirv-dis on it
#ifdef __unix__
          auto cmd = fmt::format("spirv-dis {}", dumpPath);
          system(cmd.c_str());
#endif
          unlink(dumpPath.c_str());
          ren::ShaderReflection refl;
          refl.parseFromSpirv(reinterpret_cast<const u8*>(module->getCode().data()),
                              module->getCode().size() * sizeof(u32));
          // ren::println("Reflection:\n{}", refl.getRoot()->toJson().dump(2));

          for (const auto& b : refl.bindings) {
            ren::println("Binding: set {} binding {} name {} type {}", b.set, b.index, b.path,
                         static_cast<int>(b.node->type.type));
          }
        }
      }
      ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::Text("Shader Reflection:");
    reflection->inspect();

    // ImGui::Text("Slang Program");
    if (slangProgram.get() != NULL) inspectSlangComponent(slangProgram.get());

    if (ImGui::BeginTable("##ShaderProgramBindings", 6, flags)) {
      auto colFlags = ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_WidthStretch;
      ImGui::TableSetupColumn("Set", colFlags);
      ImGui::TableSetupColumn("Binding", colFlags);
      ImGui::TableSetupColumn("Name", colFlags);
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
        ImGui::Text("%s", binding.name.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%d", binding.type);
        ImGui::TableNextColumn();
        ImGui::Text("%u", binding.count);
        ImGui::TableNextColumn();
        ImGui::Text("%u", binding.stages);
      }
      ImGui::EndTable();
    }

    ImGui::Separator();
  }



  ref<ShaderObject> ShaderProgram::instantiate() {
    auto& frame = getFrameData();
    return make<ShaderObject>(this->shared_from_this(), frame.descriptorAllocator);
  }


  ShaderObject::ShaderObject(ref<ShaderProgram> program, DescriptorAllocator& descAlloc)
      : program(program) {
    const auto& layouts = program->getDescriptorSetLayouts();

    for (size_t i = 0; i < layouts.size(); i++) {
      if (layouts[i] == VK_NULL_HANDLE) {
        sets.push_back(VK_NULL_HANDLE);
        continue;
      }

      VkDescriptorSet descriptorSet;
      bool success = descAlloc.allocate(&descriptorSet, layouts[i]);
      if (!success) { throw std::runtime_error("Failed to allocate descriptor set"); }
      sets.push_back(descriptorSet);
    }
  }


  ShaderObject::~ShaderObject() {
    auto& vulkan = ren::getVulkan();

    // Descriptor sets are freed when the descriptor pool is reset.

    // That said, if we ever wanted to free them individually, we could do so here:
    // But I'm not sure we want to do that just yet.
    // vkFreeDescriptorSets(vulkan.device, vulkan.descriptorPool, sets.size(), sets.data());
  }

}  // namespace ren
