#pragma once

#include <string>
#include <ren/renderer/Shader.h>

namespace ren {



  // Whenever you want a pipeline, you should create one of these instead,
  // and ask the pipeline cache to get you a pipeline from it.
  struct PipelineDescription {
    std::string name = "pipeline";

    // For now, we explicity state the shaders used in the pipeline as two stages.
    ref<Shader> vertexShader;
    ref<Shader> fragmentShader;

    inline PipelineDescription(std::string name, ref<Shader> vertexShader,
                               ref<Shader> fragmentShader)
        : name(name)
        , vertexShader(vertexShader)
        , fragmentShader(fragmentShader) {}
  };

}  // namespace ren