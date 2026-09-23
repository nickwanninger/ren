#include <ren/renderer/shader/ShaderCache.h>
#include <ren/renderer/shader/ShaderReflection.h>
#include <ren/renderer/shader/SlangFileSystem.h>
#include <ren/renderer/Renderer.h>
#include <ren/core/Flag.h>
#include <ren/core/Instrumentation.h>
#include <fmt/format.h>
#include <imgui/imgui.h>
#include <algorithm>
#include <cctype>
#include <chrono>

namespace ren {

  static ren::Flag<int> kSlangOptLevel("slang-opt-level", 2, "Optimization level for Slang compiler (0=None, 1=Default, 2=Maximum)");

  namespace {
    // Helper to check diagnostics
    void checkSlangDiagnostics(slang::IBlob* diagnosticsBlob) {
      if (diagnosticsBlob && diagnosticsBlob->getBufferSize() > 0) {
        const char* msg = (const char*)diagnosticsBlob->getBufferPointer();
        throw std::runtime_error(std::string("Slang compilation error: ") + msg);
      }
    }

    // Helper to convert Slang stage to Vulkan stage
    VkShaderStageFlagBits slangStageToVulkan(SlangStage stage) {
      switch (stage) {
        case SLANG_STAGE_VERTEX:
          return VK_SHADER_STAGE_VERTEX_BIT;
        case SLANG_STAGE_FRAGMENT:
          return VK_SHADER_STAGE_FRAGMENT_BIT;
        case SLANG_STAGE_COMPUTE:
          return VK_SHADER_STAGE_COMPUTE_BIT;
        case SLANG_STAGE_GEOMETRY:
          return VK_SHADER_STAGE_GEOMETRY_BIT;
        case SLANG_STAGE_HULL:
          return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        case SLANG_STAGE_DOMAIN:
          return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        default:
          throw std::runtime_error("Unsupported shader stage");
      }
    }

    std::string toLower(std::string_view s) {
      std::string out(s);
      std::transform(out.begin(), out.end(), out.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      return out;
    }
  }  // namespace


  ShaderCache::ShaderCache() = default;

  ShaderCache::~ShaderCache() {
    // Programs own VkPipelineLayouts, so they must die while the device is
    // still alive. Drop them before the Slang session that produced them.
    entries.clear();
  }


  std::string ShaderCache::canonicalize(std::string_view path) {
    std::string s(path);
    std::replace(s.begin(), s.end(), '\\', '/');

    // `demo/cube` and `demo/cube.slang` name the same module.
    if (s.ends_with(".slang")) {
      s.resize(s.size() - 6);
    }

    // Normalize away empty, `.`, and `..` segments.
    std::vector<std::string_view> parts;
    size_t i = 0;
    while (i <= s.size()) {
      size_t j = s.find('/', i);
      if (j == std::string::npos) j = s.size();
      std::string_view seg(s.data() + i, j - i);
      if (seg.empty() || seg == ".") {
        // skip
      } else if (seg == ".." && !parts.empty() && parts.back() != "..") {
        parts.pop_back();
      } else {
        parts.push_back(seg);
      }
      if (j == s.size()) break;
      i = j + 1;
    }

    // The session already searches the shader root, so a leading `shaders/` is
    // redundant and must not produce a second cache entry.
    if (!parts.empty() && parts.front() == "shaders") {
      parts.erase(parts.begin());
    }

    std::string out;
    for (size_t k = 0; k < parts.size(); k++) {
      if (k > 0) out += '/';
      out.append(parts[k]);
    }
    return out;
  }


  bool ShaderCache::ensureSession(void) {
    if (session && sessionOptLevel == (int)kSlangOptLevel) {
      return false;
    }

    // First, we need to make sure the global session exists.
    if (globalSession.get() == nullptr) {
      REN_PROFILE_SCOPE("CreateGlobalSession");
      if (SLANG_FAILED(slang::createGlobalSession(globalSession.writeRef()))) {
        throw std::runtime_error("Failed to create Slang global session");
      }
    }

    // A single session shared by every program in the engine, so that common
    // modules (`ren/core.slang` and friends) are parsed exactly once.
    REN_PROFILE_SCOPE("CreateSlangSession");

    slang::SessionDesc sessionDesc = {};
    slang::TargetDesc targetDesc = {};
    targetDesc.format = SLANG_SPIRV;
    targetDesc.profile = globalSession->findProfile("spirv_1_5");

    // Enable optimization
    slang::CompilerOptionEntry optimizationOption = {};
    optimizationOption.name = slang::CompilerOptionName::Optimization;
    optimizationOption.value.kind = slang::CompilerOptionValueKind::Int;
    optimizationOption.value.intValue0 = kSlangOptLevel;  // 0=None, 1=Default, 2=Maximum

    std::vector<slang::CompilerOptionEntry> compilerOptions;
    compilerOptions.push_back(optimizationOption);

    targetDesc.compilerOptionEntries = compilerOptions.data();
    targetDesc.compilerOptionEntryCount = static_cast<SlangInt>(compilerOptions.size());

    sessionDesc.targets = &targetDesc;
    sessionDesc.targetCount = 1;
    sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;

    static SlangFileSystem fileSystem;
    sessionDesc.fileSystem = &fileSystem;

    const char* searchPaths[] = {"shaders"};
    sessionDesc.searchPaths = searchPaths;
    sessionDesc.searchPathCount = 1;

    session = nullptr;
    if (SLANG_FAILED(globalSession->createSession(sessionDesc, session.writeRef()))) {
      throw std::runtime_error("Failed to create Slang session");
    }
    sessionOptLevel = kSlangOptLevel;
    return true;
  }


  CompiledProgram ShaderCache::compile(const std::string& canonicalPath) {
    REN_PROFILE_SCOPE("CompileShader");

    Slang::ComPtr<slang::IBlob> diagnosticsBlob;
    Slang::ComPtr<slang::IModule> module;

    // Load the module from the filepath.
    {
      REN_PROFILE_SCOPE("LoadSlangModule");
      module = session->loadModule(canonicalPath.c_str(), diagnosticsBlob.writeRef());
      if (!module) {
        checkSlangDiagnostics(diagnosticsBlob);
        throw std::runtime_error(fmt::format("Failed to load Slang module '{}'", canonicalPath));
      }
    }

    // Get the entry point count.
    SlangInt32 entryPointCount = module->getDefinedEntryPointCount();
    if (entryPointCount == 0) {
      throw std::runtime_error(fmt::format("No entry points found in shader '{}'", canonicalPath));
    }

    std::vector<slang::IComponentType*> allComponents;
    allComponents.push_back(module);

    for (SlangInt32 i = 0; i < entryPointCount; ++i) {
      REN_PROFILE_SCOPE("CompileSlangEntryPoint");
      Slang::ComPtr<slang::IEntryPoint> entryPoint;

      {
        REN_PROFILE_SCOPE("GetDefinedEntryPoint");
        if (SLANG_FAILED(module->getDefinedEntryPoint(i, entryPoint.writeRef()))) {
          throw std::runtime_error("Failed to get entry point");
        }
      }

      // Compose the program (module + entry point)
      allComponents.push_back(entryPoint);
    }

    // Link the modules.
    Slang::ComPtr<slang::IComponentType> program;
    {
      Slang::ComPtr<slang::IComponentType> unlinkedProgram;
      if (SLANG_FAILED(session->createCompositeComponentType(
              allComponents.data(), allComponents.size(), unlinkedProgram.writeRef()))) {
        checkSlangDiagnostics(diagnosticsBlob);
        throw std::runtime_error("Failed to create composite program");
      }

      if (SLANG_FAILED(unlinkedProgram->link(program.writeRef(), diagnosticsBlob.writeRef()))) {
        checkSlangDiagnostics(diagnosticsBlob);
        throw std::runtime_error("Failed to link program");
      }
    }

    CompiledProgram compiled;
    compiled.path = canonicalPath;

    for (SlangInt32 i = 0; i < entryPointCount; ++i) {
      REN_PROFILE_SCOPE("GetEntryPointCode");

      Slang::ComPtr<slang::IBlob> spirvCode;
      if (SLANG_FAILED(program->getEntryPointCode(i,  // entry point index
                                                  0,  // target index
                                                  spirvCode.writeRef(), diagnosticsBlob.writeRef()))) {
        checkSlangDiagnostics(diagnosticsBlob);
        throw std::runtime_error("Failed to get entry point code");
      }

      const u8* spirvBytes = (const u8*)spirvCode->getBufferPointer();
      size_t spirvSize = spirvCode->getBufferSize();

      auto* ep = program->getLayout()->getEntryPointByIndex(i);

      CompiledEntryPoint out;
      out.name = ep->getName() ? ep->getName() : "unknown";
      out.stage = slangStageToVulkan(ep->getStage());
      out.spirv.assign(spirvBytes, spirvBytes + spirvSize);
      compiled.entryPoints.push_back(std::move(out));
    }

    compiled.reflection = make<ren::ShaderReflection>();
    compiled.reflection->parseFromSlang(program->getLayout());

    // Record the files this compile actually touched. Nothing consumes these
    // yet, but they are what a future hot-reload has to watch.
    SlangInt32 depCount = module->getDependencyFileCount();
    for (SlangInt32 i = 0; i < depCount; ++i) {
      if (const char* dep = module->getDependencyFilePath(i)) {
        compiled.dependencies.emplace_back(dep);
      }
    }

    return compiled;
  }


  void ShaderCache::checkCaseCollision(const std::string& canonicalPath) {
    auto lowered = toLower(canonicalPath);
    auto [it, inserted] = lowercaseKeys.emplace(lowered, canonicalPath);
    if (!inserted && it->second != canonicalPath) {
      ren::println(
          "[ShaderCache] warning: '{}' and '{}' differ only by case. These are "
          "one file on macOS and two on Linux.",
          it->second, canonicalPath);
    }
  }


  ShaderCache::Entry& ShaderCache::lookup(std::string_view path) {
    auto canonical = canonicalize(path);

    // A rebuilt session invalidates everything compiled against the old one.
    if (ensureSession()) {
      entries.clear();
    }

    if (auto it = entries.find(canonical); it != entries.end()) {
      return it->second;
    }

    checkCaseCollision(canonical);

    Entry entry;
    auto start = std::chrono::steady_clock::now();
    try {
      auto compiled = compile(canonical);
      entry.dependencies = compiled.dependencies;
      // Private constructor: the cache is the only thing that builds programs.
      entry.program = ref<ShaderProgram>(new ShaderProgram(std::move(compiled)));
    } catch (const std::exception& e) {
      // Cache the failure too, so a broken shader is compiled once rather than
      // on every call.
      entry.error = e.what();
      ren::println("[ShaderCache] failed to compile '{}': {}", canonical, entry.error);
    }
    entry.compileMillis =
        std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - start).count();

    return entries.emplace(std::move(canonical), std::move(entry)).first->second;
  }


  ref<ShaderProgram> ShaderCache::get(std::string_view path) {
    auto& entry = lookup(path);
    if (!entry.program) {
      throw std::runtime_error(
          fmt::format("Shader '{}' failed to compile: {}", path, entry.error));
    }
    return entry.program;
  }


  ref<ShaderProgram> ShaderCache::tryGet(std::string_view path) {
    return lookup(path).program;
  }


  void ShaderCache::clear(void) {
    entries.clear();
    lowercaseKeys.clear();
    selected.clear();
    session = nullptr;
    sessionOptLevel = -1;
  }


  void ShaderCache::inspect(void) {
    ImGui::Text("%zu shader(s) cached", entries.size());
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
      clear();
      return;
    }

    // Left panel: the list of cached shaders.
    ImGui::BeginGroup();
    {
      ImGui::BeginChild("ShaderListPanel", ImVec2(200, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_Border);
      {
        for (const auto &[path, entry] : entries) {
          ImGui::PushID(path.c_str());
          // Failed compiles stay in the list, flagged, so they are debuggable.
          if (!entry.program) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
          }
          if (ImGui::Selectable(path.c_str(), path == selected)) {
            selected = path;
          }
          if (!entry.program) {
            ImGui::PopStyleColor();
          }
          ImGui::PopID();
        }
      }
      ImGui::EndChild();
    }
    ImGui::EndGroup();

    ImGui::SameLine();

    // Right panel: the selected program.
    ImGui::BeginGroup();
    {
      ImGui::BeginChild("ShaderDetailsPanel", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_Border);
      {
        auto it = entries.find(selected);
        if (it == entries.end()) {
          ImGui::TextDisabled("Select a shader.");
        } else {
          const auto &entry = it->second;
          ImGui::Text("%s", it->first.c_str());
          ImGui::SameLine();
          ImGui::TextDisabled("(compiled in %.2f ms)", entry.compileMillis);
          ImGui::Separator();

          if (entry.program) {
            entry.program->inspect();
          } else {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Failed to compile:");
            ImGui::TextWrapped("%s", entry.error.c_str());
          }

          if (!entry.dependencies.empty() && ImGui::TreeNode("Dependencies")) {
            for (const auto &dep : entry.dependencies) {
              ImGui::TextUnformatted(dep.c_str());
            }
            ImGui::TreePop();
          }
        }
      }
      ImGui::EndChild();
    }
    ImGui::EndGroup();
  }


  ref<ShaderProgram> getShader(std::string_view path) {
    return Renderer::get().getShaderCache().get(path);
  }

}  // namespace ren
