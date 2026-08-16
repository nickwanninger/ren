#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <ren/renderer/vulkan/Vulkan.h>
#include <ren/types.h>
#include <ren/core/UUID.h>
#include <ren/assets/Asset.h>

namespace ren {
  class VulkanInstance;




  // This class is the base class for all Vulkan shaders in the engine.
  // It's mainly responsible for managing the lifetime of the VkShaderModule
  // and providing the shader stage so the pipeline can use it.
  // ShaderProgram should be used instead of this class in most cases.
  class ShaderModule : public ren::VulkanResource, public ren::ShaderAsset {
   public:
    ShaderModule(const std::string_view &name, const std::vector<u8> &spirv, VkShaderStageFlagBits stage);
    virtual ~ShaderModule();

    const std::string &getFilename() const { return filename; }
    VkShaderModule getHandle() const { return shaderModule; }
    VkShaderStageFlagBits getStage() const { return stage; }
    auto &getCode() const { return code; }
   private:
    void initShader(const std::vector<u32> &code);

    std::vector<u32> code;
    std::string filename;
    VkShaderModule shaderModule = VK_NULL_HANDLE;
    VkShaderStageFlagBits stage;

  };

};  // namespace ren
