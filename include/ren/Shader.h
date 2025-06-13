#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <ren/types.h>

namespace ren {
  class VulkanInstance;


  // This class is the base class for all Vulkan shaders in the engine.
  // It's mainly responsible for manging the lifetime of teh VkShaderModule
  // and providing the shader stage so the pipeline can use it.
  // They should be obtained from a ShaderCache or similar system instead of
  // being created directly.
  class Shader {
   public:
    Shader(VulkanInstance &vulkan, const std::string &filename, VkShaderStageFlagBits stage);
    virtual ~Shader();

    const std::string &getFilename() const { return filename; }
    VkShaderModule getHandle() const { return shaderModule; }
    VkShaderStageFlagBits getStage() const { return stage; }

   private:
    std::vector<u8> loadShaderCode(const std::string &file_name);

    void initShader(const std::vector<u8> &code);


    std::string filename;
    VulkanInstance &vulkan;
    VkShaderModule shaderModule = VK_NULL_HANDLE;
    VkShaderStageFlagBits stage;
  };


  template <VkShaderStageFlagBits Stage>
  class VulkanStageShader : public Shader {
   public:
    VulkanStageShader(VulkanInstance &vulkan, const std::string &file_name)
        : Shader(vulkan, file_name, Stage) {}
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
