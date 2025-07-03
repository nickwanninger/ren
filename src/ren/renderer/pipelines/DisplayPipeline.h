#pragma once

#include <ren/renderer/pipelines/VulkanPipeline.h>
#include <ren/renderer/RenderPass.h>
#include <ren/renderer/Shader.h>

namespace ren {

  class DisplayPipeline : public VulkanPipeline {
   public:
    DisplayPipeline(ref<RenderPass> renderpass, ref<Shader> vertexShader,
                    ref<Shader> fragmentShader, VkDescriptorSetLayout descriptorSetLayout);

    ~DisplayPipeline() override = default;

   protected:
    ref<Shader> vertexShader;
    ref<Shader> fragmentShader;
  };

}  // namespace ren