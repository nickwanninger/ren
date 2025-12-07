#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <ren/renderer/Vulkan.h>
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
    ShaderModule(const std::string_view &filename, VkShaderStageFlagBits stage);
    virtual ~ShaderModule();

    static VkShaderStageFlagBits getStageFromFilename(const std::string_view &filename);

    const std::string &getFilename() const { return filename; }
    VkShaderModule getHandle() const { return shaderModule; }
    VkShaderStageFlagBits getStage() const { return stage; }
    auto &getCode() const { return code; }
    bool reload();

   private:
    std::vector<u32> loadShader(const std::string_view &file_name);

    void initShader(const std::vector<u32> &code);

    std::vector<u32> code;
    std::string filename;
    VkShaderModule shaderModule = VK_NULL_HANDLE;
    VkShaderStageFlagBits stage;
  };


  template <VkShaderStageFlagBits Stage>
  class VulkanStageShader : public ShaderModule {
   public:
    VulkanStageShader(const std::string &file_name)
        : ShaderModule(file_name, Stage) {}
    virtual ~VulkanStageShader() = default;
  };


  using VertexShader = VulkanStageShader<VK_SHADER_STAGE_VERTEX_BIT>;
  using FragmentShader = VulkanStageShader<VK_SHADER_STAGE_FRAGMENT_BIT>;
  using ComputeShader = VulkanStageShader<VK_SHADER_STAGE_COMPUTE_BIT>;
  using GeometryShader = VulkanStageShader<VK_SHADER_STAGE_GEOMETRY_BIT>;
  using TessellationControlShader = VulkanStageShader<VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT>;
  using TessellationEvaluationShader =
      VulkanStageShader<VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT>;
  using RaygenShader = VulkanStageShader<VK_SHADER_STAGE_RAYGEN_BIT_KHR>;
  using AnyHitShader = VulkanStageShader<VK_SHADER_STAGE_ANY_HIT_BIT_KHR>;
  using ClosestHitShader = VulkanStageShader<VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR>;
  using MissShader = VulkanStageShader<VK_SHADER_STAGE_MISS_BIT_KHR>;
  using IntersectionShader = VulkanStageShader<VK_SHADER_STAGE_INTERSECTION_BIT_KHR>;


};  // namespace ren
