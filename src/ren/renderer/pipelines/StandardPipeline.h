#pragma once


#include <ren/renderer/pipelines/VulkanPipeline.h>
#include <ren/renderer/RenderPass.h>
#include <ren/renderer/Shader.h>

namespace ren {
  class RenderPass;

  class StandardPipeline : public VulkanPipeline {
   public:
    StandardPipeline(ref<RenderPass> renderpass, ref<Shader> vertexShader, ref<Shader> fragmentShader,
                     VkDescriptorSetLayout descriptorSetLayout);

    ~StandardPipeline() override = default;

   protected:
    // These are currently just hardcoded as shaders/triangle.{frag,vert}
    ref<Shader> vertexShader;
    ref<Shader> fragmentShader;
  };

}  // namespace ren