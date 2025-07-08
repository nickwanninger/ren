#pragma once

#include <ren/renderer/ShaderProgram.h>
#include <ren/renderer/Shader.h>
#include <ren/renderer/Texture.h>
#include <ren/renderer/Descriptors.h>
#include <ren/renderer/Sampler.h>

namespace ren {

  // A shader binder is a utility class that helps us manage shader bindings.
  // It allows us to bind resources to shaders in a more convenient way.
  // It's interface follows a typical builder pattern, and allows you to
  // generate descriptor sets and bind them to a renderer just by using names,
  // using the shader program's reflection data as a blueprint.
  class ShaderBinder {
   public:
    ShaderBinder(ShaderProgram &program, u32 set);
    ~ShaderBinder() = default;

    void bind(const std::string_view &name, const Texture &texture);
    void bind(const std::string_view &name, const Image &image, Sampler &sampler);
    void bind(const std::string_view &name, const Image &image, VkFilter samplerFilter = VK_FILTER_NEAREST);

    // Build, then apply the descriptor sets
    void apply();


   private:
    u32 set;
    std::vector<VkWriteDescriptorSet> writes;

    ShaderProgram &program;

    // To bind image views, we need to keep track of the VkDescriptorImageInfo values we want to
    // bind.
    std::vector<VkDescriptorImageInfo> imageInfos;
  };



}  // namespace ren