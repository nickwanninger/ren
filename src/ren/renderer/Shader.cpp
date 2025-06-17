

#include <ren/renderer/Shader.h>
#include <ren/renderer/Vulkan.h>
#include "vulkan/vulkan_core.h"
#include <fstream>
#include <spirv_reflect/spirv_reflect.h>
#include <spirv_reflect/common.h>
#include <spirv_reflect/output_stream.h>

ren::Shader::Shader(const std::string& file_name, VkShaderStageFlagBits stage)
    : stage(stage) {
  auto code = loadShaderCode(file_name);
  initShader(code);
}
ren::Shader::~Shader() {
  auto& vulkan = ren::getVulkan();
  if (shaderModule) {
    vkDestroyShaderModule(vulkan.device, shaderModule, nullptr);
    shaderModule = VK_NULL_HANDLE;
  }
}


std::vector<u8> ren::Shader::loadShaderCode(const std::string& filename) {
  // TODO: AssetManager
  std::vector<u8> code;

  // Load the shader code from the file
  std::ifstream file(filename, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    throw std::runtime_error(fmt::format("Failed to open shader file: {}", filename));
  }
  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);
  code.resize(size);
  if (!file.read(reinterpret_cast<char*>(code.data()), size)) {
    throw std::runtime_error(fmt::format("Failed to read shader file: {}", filename));
  }
  file.close();
  fmt::print("Loading shader from {} ({} bytes)\n", filename, size);
  return code;
}

#if 0
static void print_push_constant_block(std::ostream& os, const SpvReflectBlockVariable& block,
                                      int depth = 0) {
  std::string indent(depth * 2, ' ');

  os << indent << "name:             " << block.name << "\n";
  os << indent << "spirv_id:         " << block.spirv_id << "\n";
  os << indent << "size:             " << block.size << "\n";
  os << indent << "offset:           " << block.offset << "\n";
  os << indent << "decoration_flags: " << block.decoration_flags << "\n";


  if (block.member_count != 0) {
    os << indent << "members:          " << block.member_count << "\n";
    for (int i = 0; i < block.member_count; i++) {
      print_push_constant_block(os, block.members[i], depth + 1);
    }
  }
  std::cout << "\n";
};
#endif

void ren::Shader::initShader(const std::vector<u8>& code) {
#if 0
  // Generate reflection data for a shader
  SpvReflectShaderModule module;
  SpvReflectResult result = spvReflectCreateShaderModule(code.size(), code.data(), &module);
  assert(result == SPV_REFLECT_RESULT_SUCCESS);

  // Enumerate and extract shader's input variables
  uint32_t var_count = 0;
  result = spvReflectEnumerateInputVariables(&module, &var_count, NULL);

  std::vector<SpvReflectInterfaceVariable*> vars(var_count);
  result = spvReflectEnumerateInputVariables(&module, &var_count, vars.data());

  PrintModuleInfo(std::cout, module);

  const char* t = "  ";
  const char* tt = "    ";

  std::cout << "Interface Variables:\n";
  for (size_t index = 0; index < vars.size(); ++index) {
    auto v = vars[index];
    std::cout << t << index << ":"
              << "\n";
    PrintInterfaceVariable(std::cout, module.source_language, *v, tt);
    std::cout << "\n";
  }


  u32 pcs_count = 0;
  spvReflectEnumeratePushConstantBlocks(&module, &pcs_count, NULL);
  std::vector<SpvReflectBlockVariable*> pcs(pcs_count);
  spvReflectEnumeratePushConstantBlocks(&module, &pcs_count, pcs.data());



  std::cout << "Push Constants:\n";
  for (size_t index = 0; index < pcs.size(); ++index) {
    auto& obj = *pcs[index];
    auto& os = std::cout;

    os << t << index << ":\n";

    print_push_constant_block(os, obj, 1);

    // os << tt << "name:     : " << obj.name << "\n";
    // os << tt << "offset    : " << obj.offset << "\n";
    // os << tt << "size      : " << obj.size  << "\n";
    // os << tt << "qualifier : ";
    // if (obj.decoration_flags & SPV_REFLECT_DECORATION_FLAT) {
    //   os << "flat";
    // } else if (obj.decoration_flags & SPV_REFLECT_DECORATION_NOPERSPECTIVE) {
    //   os << "noperspective";
    // } else if (obj.decoration_flags & SPV_REFLECT_DECORATION_PATCH) {
    //   os << "patch";
    // } else if (obj.decoration_flags & SPV_REFLECT_DECORATION_PER_VERTEX) {
    //   os << "pervertex";
    // } else if (obj.decoration_flags & SPV_REFLECT_DECORATION_PER_TASK) {
    //   os << "pertask";
    // } else {
    //   os << "unknown";
    // }
    // os << "\n";


    std::cout << "\n";
  }


  // Grab Descriptor sets
  uint32_t count = 0;
  result = spvReflectEnumerateDescriptorSets(&module, &count, NULL);
  assert(result == SPV_REFLECT_RESULT_SUCCESS);

  std::vector<SpvReflectDescriptorSet*> sets(count);
  result = spvReflectEnumerateDescriptorSets(&module, &count, sets.data());
  assert(result == SPV_REFLECT_RESULT_SUCCESS);


  // Log the descriptor set contents to stdout


  std::cout << "Descriptor sets:\n";
  for (size_t index = 0; index < sets.size(); ++index) {
    auto p_set = sets[index];

    // descriptor sets can also be retrieved directly from the module, by set
    // index
    auto p_set2 = spvReflectGetDescriptorSet(&module, p_set->set, &result);
    assert(result == SPV_REFLECT_RESULT_SUCCESS);
    assert(p_set == p_set2);
    (void)p_set2;

    std::cout << t << index << ":"
              << "\n";
    PrintDescriptorSet(std::cout, *p_set, tt);
    std::cout << "\n\n";
  }

  std::cout << "\n\n";
  spvReflectDestroyShaderModule(&module);

  //////////////
#endif
  auto& vulkan = ren::getVulkan();

  VkShaderModuleCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = code.size();
  createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

  if (vkCreateShaderModule(vulkan.device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
    throw std::runtime_error("failed to create shader module!");
  }
}
