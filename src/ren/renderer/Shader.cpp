

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

void ren::Shader::initShader(const std::vector<u8>& code) {

  this->code = code;
#if 1
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

  // std::cout << "Interface Variables:\n";
  // for (size_t index = 0; index < vars.size(); ++index) {
  //   auto v = vars[index];
  //   std::cout << t << index << ":"
  //             << "\n";
  //   PrintInterfaceVariable(std::cout, module.source_language, *v, tt);
  //   std::cout << "\n";
  // }


  u32 pcs_count = 0;
  spvReflectEnumeratePushConstantBlocks(&module, &pcs_count, NULL);
  std::vector<SpvReflectBlockVariable*> pcs(pcs_count);
  spvReflectEnumeratePushConstantBlocks(&module, &pcs_count, pcs.data());


  // Grab Descriptor sets
  uint32_t count = 0;
  result = spvReflectEnumerateDescriptorSets(&module, &count, NULL);
  assert(result == SPV_REFLECT_RESULT_SUCCESS);

  std::vector<SpvReflectDescriptorSet*> sets(count);
  result = spvReflectEnumerateDescriptorSets(&module, &count, sets.data());
  assert(result == SPV_REFLECT_RESULT_SUCCESS);

#endif

  //////////////

  auto& vulkan = ren::getVulkan();

  VkShaderModuleCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = code.size();
  createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

  if (vkCreateShaderModule(vulkan.device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
    throw std::runtime_error("failed to create shader module!");
  }
}
