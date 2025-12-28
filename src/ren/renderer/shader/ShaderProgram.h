#pragma once

#include <ren/types.h>
#include <ren/renderer/shader/ShaderModule.h>
#include <ren/renderer/vulkan/Vulkan.h>
#include <ren/renderer/Descriptors.h>
#include <spirv_reflect/spirv_reflect.h>
#include <ren/misc/json_serialize.h>
#include <ren/renderer/shader/SlangCompiler.h>
#include <ren/core/File.h>
#include <ren/renderer/shader/ShaderReflection.h>
#include <vector>

namespace ren {

  /**
   * The shader system in REN has three main parts.
   *
   * - ShaderProgram: represents a set of shaders which are all bound together to form a pipeline
   * - ShaderModule: represents a single Vulkan shader module (VkShaderModule) loaded from SPIR-V
   * - ShaderObject: represents a single instance of a shader (in particular, its descriptor sets
   *                 and any per-instance state.)
   */



  // A binding is a shader resource with a set and binding index, a type, and a name.
  struct ShaderBinding {
    std::string name;
    u32 set, binding, count;
    VkDescriptorType type;
    VkShaderStageFlags stages;
  };

  class ShaderObject;

  // A shader program represents a set of shaders that are linked together
  // to form a pipeline.  It is responsible for reflecting the shader resources
  // and creating the pipeline layout.
  class ShaderProgram : public ren::VulkanResource,
                        public ren::HasUUID,
                        public std::enable_shared_from_this<ShaderProgram> {
   public:
    // Load from slang
    ShaderProgram(const std::string& shader);

    // DEPRECATED!!!
    ShaderProgram(const std::string& glslVertexShader, const std::string& glslFragmentShader);
    ~ShaderProgram();


    static inline ref<ShaderProgram> makeFullScreenProgram(const std::string& fragmentShaderPath) {
      return make<ShaderProgram>("shaders/display.vert", fragmentShaderPath);
    }

    // -- Non-copyable, movable -- //
    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;
    ShaderProgram(ShaderProgram&&) = default;
    ShaderProgram& operator=(ShaderProgram&&) = default;


    ref<ShaderObject> instantiate();


    // -- Getters -- //
    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }
    // TODO: abstract me! We want to also handle compute shaders perhaps!
    const std::vector<ref<ShaderModule>>& getShaders() const { return shaders; }
    const std::vector<ShaderBinding>& getBindings() const { return bindings; }
    const ShaderBinding* getBinding(const std::string_view& name) const;
    const ShaderBinding* getBinding(u32 set, u32 binding) const;

    const std::vector<VkDescriptorSetLayout>& getDescriptorSetLayouts() const { return setLayouts; }

    auto getReflection() const { return reflection; }


    // Inspect the shader program in imgui.
    void inspect(void);

    // Serialization is currently simple. We need to be smarter about reading this back out, though.
    JSON_SERIALIZE(ShaderProgram, vertexShaderPath, fragmentShaderPath);

   private:
    std::string vertexShaderPath;    // TODO(NUKE)
    std::string fragmentShaderPath;  // TODO(NUKE)
    void reflectShaders();
    void reflectShader(const std::vector<u32>& spirv, VkShaderStageFlagBits stage);


    void mergeDescriptorBindings();
    void bakeLayouts();

    std::vector<ShaderBinding> bindings;
    ref<ren::ShaderReflection> reflection;

    // Eventually, we generate a pipeline layout from the shader reflection.
    std::vector<VkDescriptorSetLayout> setLayouts;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    std::vector<ref<ShaderModule>> shaders;
    Slang::ComPtr<slang::IComponentType> slangProgram;
  };


  class ShaderObject {
   public:
    ShaderObject(ref<ShaderProgram> program, DescriptorAllocator& descAlloc);
    ~ShaderObject();


    VkPipelineLayout getLayout() const { return program->getPipelineLayout(); }

    const std::vector<VkDescriptorSet>& getDescriptorSets() const { return sets; }

    auto getReflection(void) const { return program->getReflection(); }

   private:
    ref<ShaderProgram> program;
    std::vector<VkDescriptorSet> sets;
  };



}  // namespace ren
