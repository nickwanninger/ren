#pragma once


#include <ren/renderer/pipelines/VulkanPipeline.h>
#include <ren/renderer/Shader.h>

namespace ren {

  class StandardPipeline : public VulkanPipeline {
   public:
    StandardPipeline(std::shared_ptr<Shader> vertexShader, std::shared_ptr<Shader> fragmentShader,
                     VkDescriptorSetLayout descriptorSetLayout);

    ~StandardPipeline() override = default;

   protected:
    // These are currently just hardcoded as shaders/triangle.{frag,vert}
    std::shared_ptr<Shader> vertexShader;
    std::shared_ptr<Shader> fragmentShader;
  };

}  // namespace ren