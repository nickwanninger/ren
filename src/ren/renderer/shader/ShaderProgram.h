#pragma once

#include <ren/types.h>
#include <ren/renderer/shader/ShaderModule.h>
#include <ren/renderer/vulkan/Vulkan.h>
#include <ren/renderer/Descriptors.h>
#include <spirv_reflect/spirv_reflect.h>
#include <ren/misc/json_serialize.h>
#include <ren/renderer/shader/ShaderReflection.h>
#include <vector>

namespace ren {

  /**
   * The shader system in REN has four main parts.
   *
   * - ShaderCache:   compiles shaders on demand and owns every ShaderProgram.
   * - ShaderProgram: represents a set of shaders which are all bound together to form a pipeline
   * - ShaderModule:  represents a single Vulkan shader module (VkShaderModule) loaded from SPIR-V
   * - ShaderCursor:  represents reflected push-constant state for one command binding.
   */

  class ShaderCache;


  // A binding is a shader resource with a set and binding index, a type, and a name.
  struct ShaderBinding {
    std::string name;
    u32 set, binding, count;
    VkDescriptorType type;
    VkShaderStageFlags stages;
  };


  // One compiled entry point, as produced by the shader compiler front end.
  struct CompiledEntryPoint {
    std::string name;
    VkShaderStageFlagBits stage;
    std::vector<u8> spirv;
  };

  // The complete output of compiling one shader file. This is the hand-off
  // between ShaderCache (which knows about Slang) and ShaderProgram (which
  // knows about Vulkan) and is the only thing a ShaderProgram is built from.
  struct CompiledProgram {
    std::string path;  // canonical cache key the program was compiled from
    std::vector<CompiledEntryPoint> entryPoints;
    ref<ren::ShaderReflection> reflection;
    std::vector<std::string> dependencies;  // source files the compile touched
  };


  // A shader program represents a set of shaders that are linked together
  // to form a pipeline.  It is responsible for reflecting the shader resources
  // and creating the pipeline layout.
  //
  // Programs are not constructed directly: ask the cache for one with
  // `ren::getShader("demo/cube")`.
  class ShaderProgram : public ren::VulkanResource,
                        public ren::HasUUID,
                        public std::enable_shared_from_this<ShaderProgram> {
   public:
    ~ShaderProgram();

    // -- Non-copyable, movable -- //
    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;
    ShaderProgram(ShaderProgram&&) = default;
    ShaderProgram& operator=(ShaderProgram&&) = default;



    // -- Getters -- //
    const std::string& getPath() const { return shaderPath; }
    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }
    const std::vector<ref<ShaderModule>>& getShaders() const { return shaders; }
    const std::vector<ShaderBinding>& getBindings() const { return bindings; }
    const ShaderBinding* getBinding(const std::string_view& name) const;
    const ShaderBinding* getBinding(u32 set, u32 binding) const;

    const std::vector<VkDescriptorSetLayout>& getDescriptorSetLayouts() const { return setLayouts; }

    auto getReflection() const { return reflection; }


    // Inspect the shader program in imgui.
    void inspect(void);

    // Runtime compilation is the source of truth. Serialization records only
    // the Slang module path; compiled-module caching is intentionally deferred.
    JSON_SERIALIZE(ShaderProgram, shaderPath);

    void temporarySerialize(const std::string_view &outDir);

   private:
    // Only the cache builds programs, so that every shader in the engine is
    // compiled exactly once and shared.
    friend class ShaderCache;
    explicit ShaderProgram(CompiledProgram&& compiled);

    std::string shaderPath;
    void bakeLayouts();

    // Turn compiled SPIR-V into VkShaderModules and validate the emitted
    // descriptor bindings against REN's global ABI.
    void createModules(const std::vector<CompiledEntryPoint>& entryPoints);
    void validateAgainstABI(void) const;

    std::vector<ShaderBinding> bindings;
    ref<ren::ShaderReflection> reflection;

    // Eventually, we generate a pipeline layout from the shader reflection.
    std::vector<VkDescriptorSetLayout> setLayouts;
    std::vector<VkPushConstantRange> pushConstantRanges;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    std::vector<ref<ShaderModule>> shaders;
  };



}  // namespace ren
