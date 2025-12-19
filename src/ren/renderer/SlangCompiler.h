#pragma once
#include <ren/renderer/ShaderReflection.h>

namespace ren {



  struct SlangCompilationResult {
    struct Module {
      std::string name;
      std::vector<u8> spirv;
      VkShaderStageFlagBits stage;
    };
    ref<ShaderReflection> reflection;
    std::vector<Module> modules;
  };

  SlangCompilationResult compileSlangShaders(const char *slangFilePath);
}  // namespace ren