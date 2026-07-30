#include "SlangTestHarness.h"

#include <atomic>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace ren::test {
  namespace {

    std::string diagnosticsText(slang::IBlob* diagnostics) {
      if (diagnostics == nullptr || diagnostics->getBufferSize() == 0) {
        return {};
      }

      const auto* data = static_cast<const char*>(diagnostics->getBufferPointer());
      return std::string(data, diagnostics->getBufferSize());
    }

    [[noreturn]] void throwSlangError(std::string_view operation, slang::IBlob* diagnostics) {
      auto text = diagnosticsText(diagnostics);
      if (text.empty()) {
        text = "Slang returned no diagnostics";
      }
      throw std::runtime_error(std::string(operation) + ": " + text);
    }

  }  // namespace

  slang::ProgramLayout* CompiledSlangProgram::layout() const {
    return program != nullptr ? program->getLayout() : nullptr;
  }

  std::vector<std::byte> CompiledSlangProgram::entryPointSpirv(std::size_t index) const {
    if (program == nullptr || index >= entryPointNames.size()) {
      throw std::out_of_range("Slang entry-point index is out of range");
    }

    Slang::ComPtr<slang::IBlob> code;
    Slang::ComPtr<slang::IBlob> diagnostics;
    if (SLANG_FAILED(program->getEntryPointCode(
            static_cast<SlangInt>(index), 0, code.writeRef(), diagnostics.writeRef()))) {
      throwSlangError("Failed to generate entry-point SPIR-V", diagnostics);
    }

    std::vector<std::byte> result(code->getBufferSize());
    std::memcpy(result.data(), code->getBufferPointer(), result.size());
    return result;
  }

  CompiledSlangProgram compileSlangSource(
      std::string_view source,
      std::string_view testName,
      SlangTestOptions options) {
    CompiledSlangProgram result;

    if (SLANG_FAILED(slang::createGlobalSession(result.globalSession.writeRef()))) {
      throw std::runtime_error("Failed to create Slang global session");
    }

    slang::CompilerOptionEntry compilerOptions[3] = {};
    compilerOptions[0].name = slang::CompilerOptionName::VulkanEmitReflection;
    compilerOptions[0].value.kind = slang::CompilerOptionValueKind::Int;
    compilerOptions[0].value.intValue0 = 1;
    compilerOptions[1].name = slang::CompilerOptionName::MatrixLayoutColumn;
    compilerOptions[1].value.kind = slang::CompilerOptionValueKind::Int;
    compilerOptions[1].value.intValue0 = 1;
    compilerOptions[2].name = slang::CompilerOptionName::Optimization;
    compilerOptions[2].value.kind = slang::CompilerOptionValueKind::Int;
    compilerOptions[2].value.intValue0 = options.optimization;

    slang::TargetDesc target = {};
    target.format = SLANG_SPIRV;
    target.profile = result.globalSession->findProfile("spirv_1_5");
    target.compilerOptionEntries = compilerOptions;
    target.compilerOptionEntryCount = 3;

    slang::SessionDesc sessionDesc = {};
    sessionDesc.targets = &target;
    sessionDesc.targetCount = 1;
    if (SLANG_FAILED(result.globalSession->createSession(
            sessionDesc, result.session.writeRef()))) {
      throw std::runtime_error("Failed to create Slang test session");
    }

    // Module names are unique even when a fixture compiles multiple variants
    // in one process. Slang sessions cache modules by name.
    static std::atomic_uint64_t nextModuleId = 0;
    const auto id = nextModuleId.fetch_add(1, std::memory_order_relaxed);
    const auto moduleName = std::string(testName) + "_" + std::to_string(id);
    const auto modulePath = moduleName + ".slang";
    const std::string sourceText(source);

    Slang::ComPtr<slang::IBlob> diagnostics;
    Slang::ComPtr<slang::IModule> module;
    module = result.session->loadModuleFromSourceString(
        moduleName.c_str(), modulePath.c_str(), sourceText.c_str(), diagnostics.writeRef());
    if (module == nullptr) {
      throwSlangError("Failed to load inline Slang module", diagnostics);
    }

    std::vector<Slang::ComPtr<slang::IEntryPoint>> entryPoints;
    std::vector<slang::IComponentType*> components = {module.get()};
    const auto entryPointCount = module->getDefinedEntryPointCount();
    entryPoints.reserve(entryPointCount);
    result.entryPointNames.reserve(entryPointCount);

    for (SlangInt32 i = 0; i < entryPointCount; ++i) {
      Slang::ComPtr<slang::IEntryPoint> entryPoint;
      if (SLANG_FAILED(module->getDefinedEntryPoint(i, entryPoint.writeRef()))) {
        throw std::runtime_error("Failed to obtain inline Slang entry point");
      }

      result.entryPointNames.emplace_back(entryPoint->getFunctionReflection()->getName());
      components.push_back(entryPoint.get());
      entryPoints.push_back(std::move(entryPoint));
    }

    if (entryPoints.empty()) {
      throw std::runtime_error("Inline Slang test source defines no entry points");
    }

    Slang::ComPtr<slang::IComponentType> composite;
    if (SLANG_FAILED(result.session->createCompositeComponentType(
            components.data(), static_cast<SlangInt>(components.size()),
            composite.writeRef(), diagnostics.writeRef()))) {
      throwSlangError("Failed to compose inline Slang program", diagnostics);
    }

    if (SLANG_FAILED(composite->link(result.program.writeRef(), diagnostics.writeRef()))) {
      throwSlangError("Failed to link inline Slang program", diagnostics);
    }

    return result;
  }

}  // namespace ren::test
