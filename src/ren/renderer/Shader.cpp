

#include <ren/renderer/Shader.h>
#include <ren/renderer/Vulkan.h>
#include "vulkan/vulkan_core.h"
#include <fstream>
#include <spirv_reflect/spirv_reflect.h>
#include <spirv_reflect/common.h>
#include <spirv_reflect/output_stream.h>

#include <shaderc/shaderc.hpp>

ren::Shader::Shader(const std::string_view& file_name, VkShaderStageFlagBits stage)
    : stage(stage) {
  auto code = loadShader(file_name);
  initShader(code);
}
ren::Shader::~Shader() {
  auto& vulkan = ren::getVulkan();
  if (shaderModule) {
    vkDestroyShaderModule(vulkan.device, shaderModule, nullptr);
    shaderModule = VK_NULL_HANDLE;
  }
}


VkShaderStageFlagBits ren::Shader::getStageFromFilename(const std::string_view& filename) {

  #define CASE(ext, stage) if (filename.ends_with(ext) || filename.ends_with(ext ".spv")) return stage;

  CASE(".vert", VK_SHADER_STAGE_VERTEX_BIT);
  CASE(".frag", VK_SHADER_STAGE_FRAGMENT_BIT);
  CASE(".comp", VK_SHADER_STAGE_COMPUTE_BIT);
  CASE(".compute", VK_SHADER_STAGE_COMPUTE_BIT);


  throw std::runtime_error(fmt::format("Unknown shader stage for file: {}", filename));
}


std::vector<u32> ren::Shader::loadShader(const std::string_view& filename) {
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
    // Load the SPIR-V binary from the file.
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
      throw std::runtime_error(fmt::format("Failed to open shader file: {}", filename));
    }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    code.resize(size / sizeof(u32));
    if (!file.read(reinterpret_cast<char*>(code.data()), size)) {
      throw std::runtime_error(fmt::format("Failed to read shader file: {}", filename));
    }
    file.close();
    fmt::print("Loading shader from {} ({} bytes)\n", filename, size);
  } else {

    // Otherwise, we'll compile it from source using shaderc from the vulkan sdk.
    REN_PROFILE_SCOPE("Compile Shader from Source");

    shaderc::Compiler compiler;
    shaderc::CompileOptions options;

    // Set compilation options
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);

    // Don't remove bindings.
    options.SetPreserveBindings(true);
    options.SetAutoMapLocations(true);
    options.SetGenerateDebugInfo();  // Preserve debug info for reflection

    // options.SetOptimizationLevel(shaderc_optimization_level_performance);
    options.SetOptimizationLevel(shaderc_optimization_level_zero);


    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
      throw std::runtime_error(fmt::format("Failed to open shader file: {}", path.string()));
    }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> sourceCode;
    sourceCode.resize(size);

    if (!file.read(reinterpret_cast<char*>(sourceCode.data()), size)) {
      throw std::runtime_error(fmt::format("Failed to read shader file: {}", path.string()));
    }
    file.close();


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


    auto result = compiler.CompileGlslToSpv(sourceCode.data(), sourceCode.size(), shaderKind,
                                            path.filename().string().c_str(), options);

    if (result.GetCompilationStatus() == shaderc_compilation_status_success) {
      fmt::print("Shader compiled successfully: {}\n", path.string());

      // copy result.cbegin() to result.cend() into code
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

void ren::Shader::initShader(const std::vector<u32>& spirv) {
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
