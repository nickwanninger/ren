#include <ren/renderer/ShaderProgram.h>
#include <algorithm>
#include <ren/assets/AssetManager.h>
#include <fmt/format.h>
#include <imgui/imgui.h>

namespace ren {

  ShaderProgram::ShaderProgram(const std::string& shaderPrefix)
      : ShaderProgram(shaderPrefix + ".vert", shaderPrefix + ".frag") {}

  ShaderProgram::ShaderProgram(const std::string& vertexPath, const std::string& fragmentPath)
      : vertexShaderPath(vertexPath)
      , fragmentShaderPath(fragmentPath) {
    this->vertexShader = ren::getAsset<VertexShader>(vertexPath);
    this->fragmentShader = ren::getAsset<FragmentShader>(fragmentPath);

    assert(vertexShader != NULL);
    assert(fragmentShader != NULL);

    reflectShaders();
    bakeLayouts();
    // createPipelineLayout();
    // createDescriptorPool();
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
    // Reflect vertex shader
    const auto& vertexCode = vertexShader->getCode();
    reflectShader(vertexCode, VK_SHADER_STAGE_VERTEX_BIT);

    // Reflect fragment shader
    const auto& fragmentCode = fragmentShader->getCode();
    reflectShader(fragmentCode, VK_SHADER_STAGE_FRAGMENT_BIT);

    // Merge and deduplicate bindings
    mergeDescriptorBindings();
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

    // Convert to our format with basic hardening
    for (const auto* binding : reflectionBindings) {
      ShaderBinding desc;
      desc.set = binding->set;
      desc.binding = binding->binding;
      desc.type = static_cast<VkDescriptorType>(binding->descriptor_type);
      // If reflection reports 0 (runtime array), use 1 for layout creation; actual arraying handled
      // by user
      desc.count = binding->count == 0 ? 1u : binding->count;
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

    // Print a concise binding listing; allow sparse sets/bindings
    printf("Baking shader program.\nBinding listing (sparse supported):\n");
    for (const auto& binding : bindings) {
      const char* typeName = "???";
      if (binding.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        typeName = "SAMPLER";
      else if (binding.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        typeName = "UNIFORM";
      else if (binding.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        typeName = "STORAGE";
      printf(" %d.%d   %-25s : %-8s x%-2u  %c%c\n", binding.set, binding.binding,
             binding.name.c_str(), typeName, binding.count,
             binding.stages & VK_SHADER_STAGE_VERTEX_BIT ? 'v' : '-',
             binding.stages & VK_SHADER_STAGE_FRAGMENT_BIT ? 'f' : '-');
    }

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
    pushConstants.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

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
    ImGui::Text("Shader Program: %s", vertexShaderPath.c_str());
    ImGui::Text("Fragment Shader: %s", fragmentShaderPath.c_str());
    ImGui::Text("Bindings: %zu", bindings.size());
    for (const auto& binding : bindings) {
      ImGui::Text("  %s: %d.%d", binding.name.c_str(), binding.set, binding.binding);
    }
  }

}  // namespace ren
