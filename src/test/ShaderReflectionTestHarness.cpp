#include "ShaderReflectionTestHarness.h"

#include <spirv_reflect/spirv_reflect.h>

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace ren::test {
  namespace {

    std::string bindingLabel(BindingKey key) {
      return "set " + std::to_string(key.set) +
             ", binding " + std::to_string(key.binding);
    }

    PhysicalProgramReflection reflectPhysicalBindings(
        const CompiledSlangProgram& compiled,
        std::vector<std::vector<std::byte>>& spirvModules) {
      PhysicalProgramReflection result;
      spirvModules.reserve(compiled.entryPointNames.size());

      for (std::size_t entryPointIndex = 0;
           entryPointIndex < compiled.entryPointNames.size();
           ++entryPointIndex) {
        spirvModules.push_back(compiled.entryPointSpirv(entryPointIndex));
        const auto& spirv = spirvModules.back();

        SpvReflectShaderModule module = {};
        const auto createResult =
            spvReflectCreateShaderModule(spirv.size(), spirv.data(), &module);
        if (createResult != SPV_REFLECT_RESULT_SUCCESS) {
          throw std::runtime_error(
              "SPIRV-Reflect could not parse entry point '" +
              compiled.entryPointNames[entryPointIndex] + "'");
        }

        uint32_t bindingCount = 0;
        auto enumerateResult =
            spvReflectEnumerateDescriptorBindings(&module, &bindingCount, nullptr);
        if (enumerateResult != SPV_REFLECT_RESULT_SUCCESS) {
          spvReflectDestroyShaderModule(&module);
          throw std::runtime_error("SPIRV-Reflect could not enumerate bindings");
        }

        std::vector<SpvReflectDescriptorBinding*> reflectedBindings(bindingCount);
        enumerateResult = spvReflectEnumerateDescriptorBindings(
            &module, &bindingCount, reflectedBindings.data());
        if (enumerateResult != SPV_REFLECT_RESULT_SUCCESS) {
          spvReflectDestroyShaderModule(&module);
          throw std::runtime_error("SPIRV-Reflect could not read bindings");
        }

        for (const auto* reflectedBinding : reflectedBindings) {
          const BindingKey key{
              .set = reflectedBinding->set,
              .binding = reflectedBinding->binding,
          };
          const auto physicalType =
              static_cast<VkDescriptorType>(reflectedBinding->descriptor_type);

          auto [it, inserted] = result.bindings.try_emplace(
              key,
              PhysicalBinding{
                  .key = key,
                  .type = physicalType,
                  .count = reflectedBinding->count,
                  .stages = static_cast<VkShaderStageFlags>(module.shader_stage),
                  .entryPoints = {compiled.entryPointNames[entryPointIndex]},
              });

          if (!inserted) {
            auto& existing = it->second;
            if (existing.type != physicalType ||
                existing.count != reflectedBinding->count) {
              spvReflectDestroyShaderModule(&module);
              throw std::runtime_error(
                  "Entry points disagree about physical " + bindingLabel(key));
            }
            existing.stages |= static_cast<VkShaderStageFlags>(module.shader_stage);
            existing.entryPoints.push_back(
                compiled.entryPointNames[entryPointIndex]);
          }
        }

        spvReflectDestroyShaderModule(&module);
      }

      return result;
    }

    std::map<BindingKey, const ShaderReflection::Binding*> indexEngineBindings(
        const ShaderReflection& reflection,
        std::string_view origin) {
      std::map<BindingKey, const ShaderReflection::Binding*> result;
      for (const auto& binding : reflection.bindings) {
        const BindingKey key{binding.set, binding.index};
        if (!result.emplace(key, &binding).second) {
          throw std::runtime_error(
              std::string(origin) + " contains duplicate " + bindingLabel(key));
        }
      }
      return result;
    }

    void expectPhysicalBindingMatches(
        const PhysicalBinding& physical,
        const ShaderReflection::Binding& engine,
        std::string_view engineOrigin) {
      SCOPED_TRACE(
          std::string(engineOrigin) + " " + bindingLabel(physical.key) +
          ", engine path '" + engine.path + "'");

      EXPECT_FALSE(engine.path.empty());
      EXPECT_EQ(engine.type.toVkDescriptorType(), physical.type);
      EXPECT_EQ(engine.count, physical.count);
    }

  }  // namespace

  ReflectedSlangCase ShaderReflectionTest::reflectSource(
      std::string_view source,
      SlangTestOptions options,
      std::string_view testName) {
    ReflectedSlangCase result;
    result.compiled = compileSlangSource(source, testName, options);

    result.slangReflection = std::make_unique<ShaderReflection>();
    result.slangReflection->parseFromSlang(result.compiled.layout());

    std::vector<std::vector<std::byte>> spirvModules;
    result.physical = reflectPhysicalBindings(result.compiled, spirvModules);

    result.spirvReflection = std::make_unique<ShaderReflection>();
    for (const auto& spirv : spirvModules) {
      result.spirvReflection->parseFromSpirv(
          reinterpret_cast<const uint8_t*>(spirv.data()), spirv.size());
    }

    return result;
  }

  void ShaderReflectionTest::expectBindingsConsistent(
      const ReflectedSlangCase& reflected,
      UnusedResourcePolicy unusedPolicy) {
    ASSERT_NE(reflected.slangReflection, nullptr);
    ASSERT_NE(reflected.slangReflection->getRoot(), nullptr);
    ASSERT_NE(reflected.spirvReflection, nullptr);
    ASSERT_NE(reflected.spirvReflection->getRoot(), nullptr);

    SCOPED_TRACE(
        "Engine Slang reflection:\n" +
        reflected.slangReflection->toJson().dump(2));
    SCOPED_TRACE(
        "Engine SPIR-V reflection:\n" +
        reflected.spirvReflection->toJson().dump(2));

    std::map<BindingKey, const ShaderReflection::Binding*> slangBindings;
    std::map<BindingKey, const ShaderReflection::Binding*> spirvBindings;
    ASSERT_NO_THROW(
        slangBindings =
            indexEngineBindings(*reflected.slangReflection, "Slang reflection"));
    ASSERT_NO_THROW(
        spirvBindings =
            indexEngineBindings(*reflected.spirvReflection, "SPIR-V reflection"));

    // The emitted binary is authoritative for the physical Vulkan ABI. Every
    // physical descriptor must be represented by both engine reflection paths.
    for (const auto& [key, physical] : reflected.physical.bindings) {
      const auto slangIt = slangBindings.find(key);
      ASSERT_NE(slangIt, slangBindings.end())
          << "Physical " << bindingLabel(key)
          << " has no engine Slang binding";
      expectPhysicalBindingMatches(physical, *slangIt->second, "Slang");

      const auto spirvIt = spirvBindings.find(key);
      ASSERT_NE(spirvIt, spirvBindings.end())
          << "Physical " << bindingLabel(key)
          << " has no engine SPIR-V binding";
      expectPhysicalBindingMatches(physical, *spirvIt->second, "SPIR-V");
    }

    // The engine SPIR-V parser must never invent bindings that are absent from
    // direct SPIRV-Reflect enumeration.
    EXPECT_EQ(spirvBindings.size(), reflected.physical.bindings.size());

    if (unusedPolicy == UnusedResourcePolicy::RequireExactMatch) {
      EXPECT_EQ(slangBindings.size(), reflected.physical.bindings.size())
          << "Unoptimized source should have an exact Slang/SPIR-V binding map";
    }
  }

}  // namespace ren::test
