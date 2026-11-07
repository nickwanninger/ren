#include <ren/renderer/shader/ShaderCache.h>
#include <ren/misc/hash.h>
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
      if (j == std::string::npos) {
        j = s.size();
      }
      std::string_view seg(s.data() + i, j - i);
      if (seg.empty() || seg == ".") {
        // skip
      } else if (seg == ".." && !parts.empty() && parts.back() != "..") {
        parts.pop_back();
      } else {
        parts.push_back(seg);
      }
      if (j == s.size()) {
        break;
      }
      i = j + 1;
    }

    // The session already searches the shader root, so a leading `shaders/` is
    // redundant and must not produce a second cache entry.
    if (!parts.empty() && parts.front() == "shaders") {
      parts.erase(parts.begin());
    }

    std::string out;
    for (size_t k = 0; k < parts.size(); k++) {
      if (k > 0) {
        out += '/';
      }
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


  CompiledProgram ShaderCache::compile(const Desc& desc) {
    REN_PROFILE_SCOPE("CompileShader");

    auto start = std::chrono::steady_clock::now();

    Slang::ComPtr<slang::IBlob> diagnosticsBlob;

    const auto& paths = desc.getModules();
    if (paths.empty()) {
      throw std::runtime_error("Shader desc has no modules");
    }

    // Load every module. The session caches modules by name, so a module that
    // is part of many programs (a surface, a pass) runs the front end one time.
    std::vector<Slang::ComPtr<slang::IModule>> modules;
    for (const auto& path : paths) {
      REN_PROFILE_SCOPE("LoadSlangModule");
      Slang::ComPtr<slang::IModule> module;
      module = session->loadModule(path.c_str(), diagnosticsBlob.writeRef());
      if (!module) {
        checkSlangDiagnostics(diagnosticsBlob);
        throw std::runtime_error(fmt::format("Failed to load Slang module '{}'", path));
      }
      modules.push_back(module);
    }

    // The composite holds all modules first, then the entry points. The link
    // step resolves each `extern` in one module against an `export` in another.
    std::vector<slang::IComponentType*> allComponents;
    for (auto& module : modules) {
      allComponents.push_back(module);
    }

    // The program is every entry point that the listed modules define. The
    // order of the modules does not matter. Entry points of imported modules
    // are not added.
    std::vector<Slang::ComPtr<slang::IEntryPoint>> entryPoints;
    for (size_t m = 0; m < modules.size(); ++m) {
      SlangInt32 count = modules[m]->getDefinedEntryPointCount();
      for (SlangInt32 i = 0; i < count; ++i) {
        Slang::ComPtr<slang::IEntryPoint> entryPoint;
        if (SLANG_FAILED(modules[m]->getDefinedEntryPoint(i, entryPoint.writeRef()))) {
          throw std::runtime_error("Failed to get entry point");
        }
        entryPoints.push_back(entryPoint);
        allComponents.push_back(entryPoint);
      }
    }

    SlangInt32 entryPointCount = (SlangInt32)entryPoints.size();
    if (entryPointCount == 0) {
      throw std::runtime_error(fmt::format("No entry points found in shader '{}'", desc.toString()));
    }

    // Link the modules.
    Slang::ComPtr<slang::IComponentType> program;
    {
      Slang::ComPtr<slang::IComponentType> unlinkedProgram;
      if (SLANG_FAILED(session->createCompositeComponentType(allComponents.data(), allComponents.size(), unlinkedProgram.writeRef()))) {
        checkSlangDiagnostics(diagnosticsBlob);
        throw std::runtime_error("Failed to create composite program");
      }

      if (SLANG_FAILED(unlinkedProgram->link(program.writeRef(), diagnosticsBlob.writeRef()))) {
        checkSlangDiagnostics(diagnosticsBlob);
        throw std::runtime_error("Failed to link program");
      }
    }

    CompiledProgram compiled;
    compiled.path = desc.toString();


    ftr_begin("shader", compiled.path.c_str());

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
    // This is the union for all modules. Shared imports such as `ren/core`
    // occur only one time.
    for (auto& module : modules) {
      SlangInt32 depCount = module->getDependencyFileCount();
      for (SlangInt32 i = 0; i < depCount; ++i) {
        const char* dep = module->getDependencyFilePath(i);
        if (dep && std::find(compiled.dependencies.begin(), compiled.dependencies.end(), dep) == compiled.dependencies.end()) {
          compiled.dependencies.emplace_back(dep);
        }
      }
    }

    ftr_end("shader", compiled.path.c_str());

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration<f64, std::milli>(end -start).count();
    ren::dbgln("Compiled '{}' in {:.2f} ms", compiled.path, duration);

    return compiled;
  }




  ShaderCache::Entry& ShaderCache::lookup(const Desc& desc) {
    // A rebuilt session invalidates everything compiled against the old one.
    if (ensureSession()) {
      entries.clear();
    }

    if (auto it = entries.find(desc); it != entries.end()) {
      return it->second;
    }

    Entry entry;
    auto start = std::chrono::steady_clock::now();
    try {
      auto compiled = compile(desc);
      entry.dependencies = compiled.dependencies;
      // Private constructor: the cache is the only thing that builds programs.
      entry.program = ref<ShaderProgram>(new ShaderProgram(std::move(compiled)));
    } catch (const std::exception& e) {
      // Cache the failure too, so a broken shader is compiled once rather than
      // on every call.
      entry.error = e.what();
      ren::println("[ShaderCache] failed to compile '{}': {}", desc.toString(), entry.error);
    }
    entry.compileMillis = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - start).count();

    return entries.emplace(desc, std::move(entry)).first->second;
  }

  ref<ShaderProgram> ShaderCache::get(const Desc& desc) {
    auto& entry = lookup(desc);
    if (!entry.program) {
      throw std::runtime_error(fmt::format("Shader '{}' failed to compile: {}", desc.toString(), entry.error));
    }
    return entry.program;
  }


  ref<ShaderProgram> ShaderCache::get(std::string_view path) {
    Desc d;
    d.addModule(path);
    return get(d);
  }



  void ShaderCache::clear(void) {
    entries.clear();
    selected.reset();
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
        for (const auto& [desc, entry] : entries) {
          auto name = desc.toString();
          ImGui::PushID(name.c_str());
          // Failed compiles stay in the list, flagged, so they are debuggable.
          if (!entry.program) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
          }
          if (ImGui::Selectable(name.c_str(), selected == desc)) {
            selected = desc;
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
        auto it = selected ? entries.find(*selected) : entries.end();
        if (it == entries.end()) {
          ImGui::TextDisabled("Select a shader.");
        } else {
          const auto& entry = it->second;
          ImGui::Text("%s", it->first.toString().c_str());
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
            for (const auto& dep : entry.dependencies) {
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


  ref<ShaderProgram> getShader(const ShaderCache::Desc& desc) { return Renderer::get().getShaderCache().get(desc); }

  void ShaderCache::Desc::addModule(std::string_view path) { modules.push_back(std::string(ShaderCache::canonicalize(path))); }


  std::string ShaderCache::Desc::toString(void) const {
    std::string out;
    for (size_t i = 0; i < modules.size(); ++i) {
      if (i > 0) {
        out += ",";
      }
      out += modules[i];
    }
    return out;
  }


  u64 ShaderCache::Desc::hash(void) const {
    u64 state = 0;
    for (const auto& module : modules) {
      hashStd(state, module);
    }
    return state;
  }

}  // namespace ren
