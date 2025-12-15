

#include <ren/renderer/ShaderModule.h>
#include <ren/renderer/Vulkan.h>
#include "vulkan/vulkan_core.h"
#include <fstream>
#include <spirv_reflect/spirv_reflect.h>
#include <spirv_reflect/common.h>
#include <spirv_reflect/output_stream.h>

#include <shaderc/shaderc.hpp>
#include <ren/assets/AssetManager.h>

// Custom includer for handling #include directives in shaders
class ShaderIncluder : public shaderc::CompileOptions::IncluderInterface {
 public:
  ShaderIncluder() = default;

  // Called when the compiler encounters a #include directive
  shaderc_include_result* GetInclude(const char* requested_source, shaderc_include_type type,
                                     const char* requesting_source, size_t include_depth) override {
    auto result = new shaderc_include_result();

    // Convert the requested source path to a string
    std::string include_path = requested_source;

    // Load the file using ren::loadAssetBytes
    auto file_contents = new std::vector<u8>();
    if (!ren::loadAssetBytes(include_path, *file_contents)) {
      result->source_name = new char[include_path.length() + 1];
      strcpy((char*)result->source_name, include_path.c_str());
      result->source_name_length = include_path.length();
      result->content = "";
      result->content_length = 0;
      return result;
    }

    // Convert the loaded bytes to a string
    auto source_str = new std::string(file_contents->begin(), file_contents->end());

    result->source_name = new char[include_path.length() + 1];
    strcpy((char*)result->source_name, include_path.c_str());
    result->source_name_length = include_path.length();
    result->content = source_str->c_str();
    result->content_length = source_str->length();

    // Store the string to prevent it from being freed
    m_loaded_sources.push_back(source_str);
    m_file_contents.push_back(file_contents);

    return result;
  }

  // Called when the compiler is done with an include
  void ReleaseInclude(shaderc_include_result* include_result) override {
    if (include_result) {
      delete[] include_result->source_name;
      delete include_result;
    }
  }

  ~ShaderIncluder() {
    // Clean up loaded sources
    for (auto str : m_loaded_sources) {
      delete str;
    }
    for (auto contents : m_file_contents) {
      delete contents;
    }
  }

 private:
  std::vector<std::string*> m_loaded_sources;
  std::vector<std::vector<u8>*> m_file_contents;
};

ren::ShaderModule::ShaderModule(const std::string_view& file_name, VkShaderStageFlagBits stage)
    : filename(file_name)
    , stage(stage) {
  if (!reload()) {
    fmt::print("Failed to load shader: {}\n", filename);
    throw std::runtime_error(fmt::format("Failed to load shader: {}", filename));
  }
}


ren::ShaderModule::~ShaderModule() {
  auto& vulkan = ren::getVulkan();
  if (shaderModule) {
    vkDestroyShaderModule(vulkan.device, shaderModule, nullptr);
    shaderModule = VK_NULL_HANDLE;
  }
}


bool ren::ShaderModule::reload() {
  try {
    auto code = loadShader(this->filename);
    initShader(code);
    return true;
  } catch (const std::exception& e) {
    fmt::print("Failed to reload shader {}: {}\n", this->filename, e.what());
    return false;
  }
}

VkShaderStageFlagBits ren::ShaderModule::getStageFromFilename(const std::string_view& filename) {
#define CASE(ext, stage) \
  if (filename.ends_with(ext) || filename.ends_with(ext ".spv")) return stage;

  CASE(".vert", VK_SHADER_STAGE_VERTEX_BIT);
  CASE(".frag", VK_SHADER_STAGE_FRAGMENT_BIT);
  CASE(".comp", VK_SHADER_STAGE_COMPUTE_BIT);
  CASE(".compute", VK_SHADER_STAGE_COMPUTE_BIT);


  throw std::runtime_error(fmt::format("Unknown shader stage for file: {}", filename));
}


std::vector<u32> ren::ShaderModule::loadShader(const std::string_view& filename) {
  REN_PROFILE_SCOPE("Load Shader");
  // This is the output code that will be returned.
  // It is either loaded from a .spv file, or compiled on the fly from GLSL.
  std::vector<u32> code;

  // if the filename contains .vert, .frag, .comp, extract the stage from it.
  std::filesystem::path path = filename;
  // If the file ends in .spv, we assume its a precompiled SPIV-V shader,
  // and we will just load that off the disk (and trust it! (eep))
  if (path.extension() == ".spv") {
    REN_PROFILE_SCOPE("Load Precompiled SPIR-V Shader");

    std::vector<u8> rawData;
    if (!ren::loadAssetBytes(filename, rawData)) {
      throw std::runtime_error(fmt::format("Failed to load shader file: {}", filename));
    }

    if (rawData.size() % sizeof(u32) != 0) {
      throw std::runtime_error(fmt::format("Invalid SPIR-V file size: {}", filename));
    }

    code.resize(rawData.size() / sizeof(u32));
    std::memcpy(code.data(), rawData.data(), rawData.size());
    fmt::print("Loading shader from {} ({} bytes)\n", filename, rawData.size());
  } else {
    // Otherwise, we'll compile it from source using shaderc from the vulkan sdk.
    REN_PROFILE_SCOPE("Compile Shader from Source");

    shaderc::Compiler compiler;
    shaderc::CompileOptions options;

    // Set up the custom includer for #include support
    options.SetIncluder(std::make_unique<ShaderIncluder>());

    // Set compilation options
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);

    // Don't remove bindings.
    options.SetPreserveBindings(true);
    options.SetAutoMapLocations(true);
    options.SetGenerateDebugInfo();  // Preserve debug info for reflection

    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    // options.SetOptimizationLevel(shaderc_optimization_level_zero);

    std::vector<u8> sourceCode;
    if (!ren::loadAssetBytes(filename, sourceCode)) {
      throw std::runtime_error(fmt::format("Failed to load shader file: {}", path.string()));
    }


    // now, try to extract the shader stage from the filename
    shaderc_shader_kind shaderKind = shaderc_glsl_infer_from_source;
    if (path.extension() == ".vert") {
      shaderKind = shaderc_glsl_vertex_shader;
    } else if (path.extension() == ".frag") {
      shaderKind = shaderc_glsl_fragment_shader;
    } else if (path.extension() == ".comp") {
      shaderKind = shaderc_glsl_compute_shader;
    } else if (path.extension() == ".geom") {
      shaderKind = shaderc_glsl_geometry_shader;
    }


    std::string shaderSource = std::string(sourceCode.begin(), sourceCode.end());
    auto result = compiler.CompileGlslToSpv((char*)sourceCode.data(), sourceCode.size(), shaderKind,
                                            path.filename().string().c_str(), options);
    if (result.GetCompilationStatus() == shaderc_compilation_status_success) {
      std::copy(result.cbegin(), result.cend(), std::back_inserter(code));
    } else if (result.GetCompilationStatus() == shaderc_compilation_status_invalid_stage) {
      throw std::runtime_error(fmt::format("Invalid shader stage for file: {}", path.string()));
    } else {
      throw std::runtime_error(
          fmt::format("Shader compilation failed: {}", result.GetErrorMessage()));
    }
  }
  return code;
}

void ren::ShaderModule::initShader(const std::vector<u32>& spirv) {
  this->code = spirv;
  //////////////

  auto& vulkan = ren::getVulkan();

  VkShaderModuleCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = code.size() * sizeof(u32);  // the number of bytes in the spirv
  createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

  if (vkCreateShaderModule(vulkan.device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
    throw std::runtime_error("failed to create shader module!");
  }
}
