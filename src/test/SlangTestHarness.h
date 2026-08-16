#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <slang-com-ptr.h>
#include <slang.h>

namespace ren::test {

  struct SlangTestOptions {
    // Keep every declared resource by default so exact reflection comparisons
    // are possible. Tests for production pruning can request MAXIMAL.
    SlangOptimizationLevel optimization = SLANG_OPTIMIZATION_LEVEL_NONE;
    // Production omits this option because it emits SPV_GOOGLE_user_type.
    // Most reflection comparison tests enable it for richer SPIR-V metadata.
    bool vulkanEmitReflection = true;
  };

  // Owns all Slang objects needed by a reflected test program. Tests can use
  // layout() as the compiler-side oracle and entryPointSpirv() as input to the
  // independent SPIRV-Reflect oracle.
  struct CompiledSlangProgram {
    Slang::ComPtr<slang::IGlobalSession> globalSession;
    Slang::ComPtr<slang::ISession> session;
    Slang::ComPtr<slang::IComponentType> program;
    std::vector<std::string> entryPointNames;

    slang::ProgramLayout* layout() const;
    std::vector<std::byte> entryPointSpirv(std::size_t index) const;
  };

  // Compile an in-memory Slang module using the same SPIR-V profile and layout
  // options as the renderer. Compilation errors are reported as exceptions
  // containing the complete Slang diagnostic text.
  CompiledSlangProgram compileSlangSource(
      std::string_view source,
      std::string_view testName = "inline_reflection_test",
      SlangTestOptions options = {});

}  // namespace ren::test
