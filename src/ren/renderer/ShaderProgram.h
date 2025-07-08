#pragma once

#include <ren/types.h>
#include <ren/renderer/Shader.h>
#include <ren/renderer/Vulkan.h>
#include <spirv_reflect/spirv_reflect.h>
#include <ren/misc/json_serialize.h>

#include <vector>
#include <unordered_map>

namespace ren {

  // A binding is a shader resource with a set and binding index, a type, and a name.
  struct ShaderBinding {
    std::string name;
    u32 set, binding, count;
    VkDescriptorType type;
    VkShaderStageFlags stages;
  };

  // A ShaderProgram is a collection of shaders that can be used to describe a
  // pipeline.  For now, we just have a vertex and fragment shader, but we can
  // extend this later if we want compute shaders.
  class ShaderProgram : public ren::VulkanResource, public ren::HasUUID {
   public:
    ShaderProgram(const std::string& shaderPrefix);
    ShaderProgram(const std::string& vertexShader, const std::string& fragmentShader);
    ~ShaderProgram();

    // -- Non-copyable, movable -- //
    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;
    ShaderProgram(ShaderProgram&&) = default;
    ShaderProgram& operator=(ShaderProgram&&) = default;


    // -- Getters -- //
    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }
    ref<Shader> getVertexShader() const { return vertexShader; }
    ref<Shader> getFragmentShader() const { return fragmentShader; }
    // TODO: abstract me! We want to also handle compute shaders perhaps!
    std::vector<ref<Shader>> getShaders() const { return {vertexShader, fragmentShader}; }
    const std::vector<ShaderBinding>& getBindings() const { return bindings; }
    const ShaderBinding* getBinding(const std::string_view& name) const;

    const std::vector<VkDescriptorSetLayout>& getDescriptorSetLayouts() const { return setLayouts; }


    // Serialization is currently simple. We need to be smarter about reading this back out, though.
    JSON_SERIALIZE(ShaderProgram, vertexShaderPath, fragmentShaderPath);

   private:
    std::string vertexShaderPath;
    std::string fragmentShaderPath;
    void reflectShaders();
    void reflectShader(const std::vector<u8>& spirv, VkShaderStageFlagBits stage);

    void mergeDescriptorBindings();
    void bakeLayouts();

    std::vector<ShaderBinding> bindings;

    // Eventually, we generate a pipeline layout from the shader reflection.
    std::vector<VkDescriptorSetLayout> setLayouts;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    ref<VertexShader> vertexShader = nullptr;
    ref<FragmentShader> fragmentShader = nullptr;
  };
}  // namespace ren