#pragma once

#include <ren/types.h>
#include <ren/renderer/shader/ShaderModule.h>
#include <ren/renderer/vulkan/Vulkan.h>
#include <ren/renderer/Descriptors.h>
#include <slang-com-ptr.h>
#include <spirv_reflect/spirv_reflect.h>
#include <ren/misc/json_serialize.h>
#include <ren/core/File.h>
#include <ren/renderer/shader/ShaderReflection.h>
#include <vector>

namespace ren {

  /**
   * The shader system in REN has three main parts.
   *
   * - ShaderProgram: represents a set of shaders which are all bound together to form a pipeline
   * - ShaderModule: represents a single Vulkan shader module (VkShaderModule) loaded from SPIR-V
   * - ShaderCursor: represents reflected push-constant state for one command binding.
   */



  // A binding is a shader resource with a set and binding index, a type, and a name.
  struct ShaderBinding {
    std::string name;
    u32 set, binding, count;
    VkDescriptorType type;
    VkShaderStageFlags stages;
  };


  // A shader program represents a set of shaders that are linked together
  // to form a pipeline.  It is responsible for reflecting the shader resources
  // and creating the pipeline layout.
  class ShaderProgram : public ren::VulkanResource,
                        public ren::HasUUID,
                        public std::enable_shared_from_this<ShaderProgram> {
   public:
    // Load from slang
    ShaderProgram(const std::string& shader);

    ~ShaderProgram();

    static inline ref<ShaderProgram> makeFullScreenProgram(const std::string& slangShaderPath) {
      return make<ShaderProgram>(slangShaderPath);
    }

    // -- Non-copyable, movable -- //
    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;
    ShaderProgram(ShaderProgram&&) = default;
    ShaderProgram& operator=(ShaderProgram&&) = default;



    // -- Getters -- //
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
    std::string shaderPath;
    void bakeLayouts();

    // Create a shader program from a slang file path, compiling the shader modules as needed.
    void compileFromSlangPath(const std::string& slangFilePath);


    Slang::ComPtr<slang::ISession> session;
    Slang::ComPtr<slang::IComponentType> program;

    std::vector<ShaderBinding> bindings;
    ref<ren::ShaderReflection> reflection;

    // Eventually, we generate a pipeline layout from the shader reflection.
    std::vector<VkDescriptorSetLayout> setLayouts;
    std::vector<VkPushConstantRange> pushConstantRanges;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    std::vector<ref<ShaderModule>> shaders;
  };



}  // namespace ren
