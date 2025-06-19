#pragma once


#include <ren/renderer/pipelines/VulkanPipeline.h>
#include <ren/renderer/Shader.h>

namespace ren {

  class PointPipeline : public VulkanPipeline {
   public:
    PointPipeline(VkDescriptorSetLayout descriptorSetLayout);

    ~PointPipeline() override = default;

   protected:
    std::shared_ptr<Shader> vertexShader;
    std::shared_ptr<Shader> geomShader;
    std::shared_ptr<Shader> fragmentShader;
  };

}  // namespace ren