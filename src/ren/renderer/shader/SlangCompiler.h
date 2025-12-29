#pragma once
#include <ren/renderer/shader/ShaderReflection.h>
#include <slang.h>
#include <slang-com-ptr.h>
#include <slang-com-helper.h>

namespace ren {


  struct SlangCompilationResult {
    struct Module {
      std::string name;
      std::vector<u8> spirv;
      VkShaderStageFlagBits stage;
    };
    std::vector<Module> modules;
    ref<ShaderReflection> reflection;
    slang::IComponentType *program;  // WARNING: LEAK?
  };

  SlangCompilationResult compileSlangShaders(const char *slangFilePath);



  void inspectSlangBindingRanges(slang::TypeLayoutReflection *programTypeLayout,
                                 slang::TypeLayoutReflection *typeLayout);

  void inspectSlangComponent(slang::IComponentType *ct);
}  // namespace ren