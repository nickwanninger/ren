#pragma once

#include <ren/renderer/pipelines/VulkanPipeline.h>
#include <ren/renderer/Shader.h>

namespace ren {

  class DisplayPipeline : public VulkanPipeline {
   public:
    DisplayPipeline(void);

    ~DisplayPipeline() override = default;

   protected:
    ref<Shader> vertexShader;
    ref<Shader> fragmentShader;
  };

}  // namespace ren