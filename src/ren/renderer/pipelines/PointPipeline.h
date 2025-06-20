#pragma once


#include <ren/renderer/pipelines/VulkanPipeline.h>
#include <ren/renderer/Shader.h>

namespace ren {

  class PointPipeline : public VulkanPipeline {
   public:
    PointPipeline(VkDescriptorSetLayout descriptorSetLayout);

    ~PointPipeline() override = default;

   protected:
    ref<Shader> vertexShader;
    ref<Shader> fragmentShader;
  };

}  // namespace ren