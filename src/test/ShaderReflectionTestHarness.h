#pragma once

#include "SlangTestHarness.h"

#include <gtest/gtest.h>
#include <ren/renderer/shader/ShaderReflection.h>

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ren::test {

  struct BindingKey {
    uint32_t set = 0;
    uint32_t binding = 0;

    auto operator<=>(const BindingKey&) const = default;
  };

  struct PhysicalBinding {
    BindingKey key;
    VkDescriptorType type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
    uint32_t count = 0;
    VkShaderStageFlags stages = 0;
    std::vector<std::string> entryPoints;
  };

  struct PhysicalProgramReflection {
    std::map<BindingKey, PhysicalBinding> bindings;
  };

  struct ReflectedSlangCase {
    CompiledSlangProgram compiled;
    std::unique_ptr<ShaderReflection> slangReflection;
    std::unique_ptr<ShaderReflection> spirvReflection;
    PhysicalProgramReflection physical;
  };

  enum class UnusedResourcePolicy {
    // Every binding in linked Slang reflection must survive in emitted SPIR-V.
    RequireExactMatch,

    // Optimized SPIR-V may omit unused Slang bindings. Every physical binding
    // must still map back to exactly one compatible engine binding.
    AllowPrunedEngineBindings,
  };

  // Base fixture for table-like inline Slang tests. reflectSource() invokes the
  // real engine parsers for both Slang ProgramLayout and every emitted SPIR-V
  // module. expectBindingsConsistent() compares those structures against an
  // independent direct SPIRV-Reflect enumeration.
  class ShaderReflectionTest : public ::testing::Test {
   protected:
    ReflectedSlangCase reflectSource(
        std::string_view source,
        SlangTestOptions options = {},
        std::string_view testName = "reflection_case");

    void expectBindingsConsistent(
        const ReflectedSlangCase& reflected,
        UnusedResourcePolicy unusedPolicy =
            UnusedResourcePolicy::RequireExactMatch);
  };

}  // namespace ren::test
