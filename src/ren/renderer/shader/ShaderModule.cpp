#include <ren/renderer/shader/ShaderModule.h>

#include <cstring>

#include <ren/renderer/vulkan/Vulkan.h>

ren::ShaderModule::ShaderModule(
    const std::string_view& name,
    const std::vector<u8>& spirv,
    VkShaderStageFlagBits stage)
    : filename(name)
    , stage(stage) {
  if (spirv.empty() || spirv.size() % sizeof(u32) != 0) {
    throw std::runtime_error("Slang emitted invalid SPIR-V bytecode");
  }
  std::vector<u32> words(spirv.size() / sizeof(u32));
  std::memcpy(words.data(), spirv.data(), spirv.size());
  initShader(words);
}

ren::ShaderModule::~ShaderModule() {
  if (shaderModule != VK_NULL_HANDLE) {
    vkDestroyShaderModule(getVulkan().device, shaderModule, nullptr);
  }
}

void ren::ShaderModule::initShader(const std::vector<u32>& spirv) {
  code = spirv;
  VkShaderModuleCreateInfo createInfo{
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = code.size() * sizeof(u32),
      .pCode = code.data(),
  };
  VK_CHECK(vkCreateShaderModule(
      getVulkan().device, &createInfo, nullptr, &shaderModule));
}
