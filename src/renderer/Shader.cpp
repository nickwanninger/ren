

#include <ren/Shader.h>
#include "vulkan/vulkan_core.h"
#include <ren/Vulkan.h>
#include <fstream>



ren::Shader::Shader(VulkanInstance &vulkan, const std::string &file_name,
                                VkShaderStageFlagBits stage)
    : vulkan(vulkan)
    , stage(stage) {
  auto code = loadShaderCode(file_name);
  initShader(code);
}
ren::Shader::~Shader() {
  if (shaderModule) {
    vkDestroyShaderModule(this->vulkan.device, shaderModule, nullptr);
    shaderModule = VK_NULL_HANDLE;
  }
}


std::vector<u8> ren::Shader::loadShaderCode(const std::string &filename) {
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
  if (!file.read(reinterpret_cast<char *>(code.data()), size)) {
    throw std::runtime_error(fmt::format("Failed to read shader file: {}", filename));
  }
  file.close();
  fmt::print("Loading shader from {} ({} bytes)\n", filename, size);
  return code;
}



void ren::Shader::initShader(const std::vector<u8> &code) {
  VkShaderModuleCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = code.size();
  createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());

  if (vkCreateShaderModule(vulkan.device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
    throw std::runtime_error("failed to create shader module!");
  }
}
