#pragma once

#include <ren/renderer/ShaderProgram.h>
#include <ren/renderer/ShaderModule.h>
#include <ren/renderer/Texture.h>
#include <ren/renderer/Descriptors.h>
#include <ren/renderer/Sampler.h>
#include <ren/renderer/Buffer.h>
#include <ren/core/Arena.h>
#include <span>
#include <utility>

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


    // The main interface is to bind a resource to a binding by name. We go through this packing
    // indirection so we can have one implementation of each ::bind method, but also let you bind by
    // index
    template <typename... T>
    ShaderBinder &bind(const std::string_view &name, const T &...resources) {
      const auto *binding = program.getBinding(name);
      if (binding == nullptr)
        throw std::runtime_error(fmt::format("ShaderModule binding '{}' not found in program '{}'", name,
                                             json(program).dump()));
      return this->bind(*binding, resources...);
    }

    // Optionally, bind by binding index within the current set.
    template <typename... T>
    ShaderBinder &bind(u32 bindingIndex, const T &...resources) {
      const auto *binding = program.getBinding(this->set, bindingIndex);
      if (binding == nullptr)
        throw std::runtime_error(fmt::format("ShaderModule binding '{}.{}' not found in program '{}'", set, bindingIndex,
                                             json(program).dump()));
      return this->bind(*binding, resources...);
    }

    ShaderBinder &bind(const ShaderBinding &binding, const Texture &texture);
    ShaderBinder &bind(const ShaderBinding &binding, const std::span<ref<Texture>> &textures);
    ShaderBinder &bind(const ShaderBinding &binding, const Image &image, Sampler &sampler);
    ShaderBinder &bind(const ShaderBinding &binding, const Image &image,
                       VkFilter samplerFilter = VK_FILTER_NEAREST);

    ShaderBinder &bind(const ShaderBinding &binding, const ren::Buffer &bufferHandle);

    // UBO set binding.
    template <typename T>
    ShaderBinder &bind(const ShaderBinding &binding, const UniformBufferSet<T> &UBS) {
      // Bind a uniform buffer set to the shader.
      this->bind(binding, UBS.currentAsBuffer());
      return *this;
    }

    // Build, then apply the descriptor sets
    void apply();


   private:
    u32 set;
    std::vector<VkWriteDescriptorSet> writes;

    ShaderProgram &program;

    // This arena is where we allocate things like VkDescriptorImageInfo and
    // VkDescriptorBufferInfo to avoid them moving (if they were stored in a
    // vector that resizes, for example).
    ren::Arena arena{512, true};
  };



}  // namespace ren
