#include <ren/renderer/ShaderProgram.h>
#include <algorithm>

namespace ren {

  ShaderProgram::ShaderProgram(const std::string& shaderPrefix)
      : ShaderProgram(shaderPrefix + ".vert.spv", shaderPrefix + ".frag.spv") {}

  ShaderProgram::ShaderProgram(const std::string& vertexPath, const std::string& fragmentPath)
      : vertexShaderPath(vertexPath)
      , fragmentShaderPath(fragmentPath) {
    this->vertexShader = makeRef<VertexShader>(vertexPath);
    this->fragmentShader = makeRef<FragmentShader>(fragmentPath);

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


  void ShaderProgram::reflectShader(const std::vector<u8>& spirv, VkShaderStageFlagBits stage) {
    SpvReflectShaderModule module;
    SpvReflectResult result =
        spvReflectCreateShaderModule(spirv.size() * sizeof(u8), (u32*)spirv.data(), &module);

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

    // Convert to our format
    for (const auto* binding : reflectionBindings) {
      ShaderBinding desc;
      desc.set = binding->set;
      desc.binding = binding->binding;
      desc.type = static_cast<VkDescriptorType>(binding->descriptor_type);
      desc.count = binding->count;
      desc.stages = stage;
      desc.name = binding->name ? binding->name : "";

      bindings.push_back(desc);
    }

    spvReflectDestroyShaderModule(&module);
  }

  void ShaderProgram::mergeDescriptorBindings() {
    // Sort by set, then binding
    std::sort(bindings.begin(), bindings.end(), [](const ShaderBinding& a, const ShaderBinding& b) {
      if (a.set != b.set) return a.set < b.set;
      return a.binding < b.binding;
    });

    // Merge duplicate bindings (same set/binding from different stages)
    std::vector<ShaderBinding> merged;
    for (const auto& binding : bindings) {
      if (!merged.empty() && merged.back().set == binding.set &&
          merged.back().binding == binding.binding) {
        // Merge stages
        merged.back().stages |= binding.stages;
      } else {
        merged.push_back(binding);
      }
    }
    bindings = std::move(merged);
  }




  void ShaderProgram::bakeLayouts() {
    auto& vulkan = getVulkan();

    printf("Baking shader program.\n");
    printf("Binding listing:\n");
    for (const auto& binding : bindings) {
      const char* typeName = "???????";
      if (binding.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) typeName = "SAMPLER";
      if (binding.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) typeName = "UNIFORM";
      // print the binding.
      printf(" %d.%d   %25s : %12s %c%c\n", binding.set, binding.binding, binding.name.c_str(),
             typeName, binding.stages & VK_SHADER_STAGE_VERTEX_BIT ? 'v' : '-',
             binding.stages & VK_SHADER_STAGE_FRAGMENT_BIT ? 'f' : '-');
    }


    // validate the bindings. if two contiguous bindings are not in contiguous sets, throw an error.
    for (size_t i = 1; i < bindings.size(); i++) {
      auto& b1 = bindings[i - 1];
      auto& b2 = bindings[i];

      // b1's set should be equal to or exactly one less than b2's set.
      if (b1.set != b2.set and b1.set != b2.set - 1) {
        throw std::runtime_error("Invalid bindings - non contiguous sets.");
      }


      // if the sets are the same, the bindings must be contiguous
      if (b1.set == b2.set and b1.binding != b2.binding - 1) {
        throw std::runtime_error("Invalid binding - non contiguous bindings.");
      }
    }

    // using our bindings, we can create a pipeline layout.

    // Group bindings by set number
    std::map<u32, std::vector<VkDescriptorSetLayoutBinding>> setBindings;

    for (const auto& binding : bindings) {
      VkDescriptorSetLayoutBinding layoutBinding{};
      layoutBinding.binding = binding.binding;
      layoutBinding.descriptorType = binding.type;
      layoutBinding.descriptorCount = binding.count;
      layoutBinding.stageFlags = binding.stages;
      layoutBinding.pImmutableSamplers = nullptr;

      setBindings[binding.set].push_back(layoutBinding);
    }

    // Create descriptor set layouts
    setLayouts.clear();
    setLayouts.resize(setBindings.rbegin()->first + 1, VK_NULL_HANDLE);


    for (const auto& [setIndex, bindings] : setBindings) {
      VkDescriptorSetLayoutCreateInfo layoutInfo{};
      layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
      layoutInfo.bindingCount = static_cast<u32>(bindings.size());
      layoutInfo.pBindings = bindings.data();

      vkCreateDescriptorSetLayout(vulkan.device, &layoutInfo, nullptr, &setLayouts[setIndex]);
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
    pipelineLayoutInfo.pSetLayouts = setLayouts.data();
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


}  // namespace ren