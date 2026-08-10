#pragma once

#include <ren/types.h>
#include <ren/renderer/vulkan/Vulkan.h>
#include <ren/renderer/shader/ShaderProgram.h>

namespace ren {
  class CommandEncoder;
  class Texture;
  class Image;
  class Sampler;

  // A command-scoped cursor into a bound program's reflection tree. Child
  // lookup and reflected write validation live here; CommandEncoder only
  // receives resolved byte locations.
  class ShaderCursor {
   public:
    ShaderCursor get(std::string_view name) const;
    ShaderCursor element(size_t index) const;
    ShaderCursor pushConstant(std::string_view name) const;

    ShaderCursor operator[](std::string_view name) const { return get(name); }

    template <typename T>
      requires std::is_trivially_copyable_v<T>
    void set(const T& value) {
      setBytes(&value, sizeof(value));
    }

    void set(const ref<Texture>& texture);
    void set(const ref<Image>& image);
    void set(const ref<Sampler>& sampler);

    template <typename T>
      requires std::is_trivially_copyable_v<T>
    ShaderCursor& set(std::string_view name, const T& value) {
      get(name).set(value);
      return *this;
    }

    ShaderCursor& set(std::string_view name, const ref<Texture>& texture) {
      get(name).set(texture);
      return *this;
    }

    ShaderCursor& set(std::string_view name, const ref<Image>& image) {
      get(name).set(image);
      return *this;
    }

    ShaderCursor& set(std::string_view name, const ref<Sampler>& sampler) {
      get(name).set(sampler);
      return *this;
    }

    ref<ShaderProgram> program() const { return m_program; }
    const ShaderReflection::Node& reflectionNode() const { return *m_node; }

   private:
    friend class CommandEncoder;
    ShaderCursor(
        CommandEncoder& encoder,
        ref<ShaderProgram> program,
        VkPipelineBindPoint bindPoint,
        u64 generation)
        : m_encoder(&encoder)
        , m_program(std::move(program))
        , m_node(m_program->getReflection()->getRoot())
        , m_bindPoint(bindPoint)
        , m_generation(generation) {}

    ShaderCursor(const ShaderCursor& parent, const ShaderReflection::Node& node)
        : m_encoder(parent.m_encoder)
        , m_program(parent.m_program)
        , m_node(&node)
        , m_bindPoint(parent.m_bindPoint)
        , m_generation(parent.m_generation) {}

    void setBytes(const void* data, size_t size);

    CommandEncoder* m_encoder;
    ref<ShaderProgram> m_program;
    const ShaderReflection::Node* m_node;
    VkPipelineBindPoint m_bindPoint;
    u64 m_generation;
  };
}  // namespace ren
