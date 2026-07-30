#pragma once

#include <ren/types.h>
#include <ren/renderer/vulkan/Vulkan.h>
#include <ren/renderer/shader/ShaderProgram.h>

namespace ren {
  class CommandEncoder;

  // A command-scoped reflected view of the currently-bound shader program.
  // It owns no descriptors or parameter storage.
  class ShaderCursor {
   public:
    template <typename T>
      requires std::is_trivially_copyable_v<T>
    ShaderCursor& set(std::string_view name, const T& value) {
      setBytes(name, &value, sizeof(value));
      return *this;
    }

    ref<ShaderProgram> program() const { return m_program; }

   private:
    friend class CommandEncoder;
    ShaderCursor(
        CommandEncoder& encoder,
        ref<ShaderProgram> program,
        VkPipelineBindPoint bindPoint,
        u64 generation)
        : m_encoder(&encoder)
        , m_program(std::move(program))
        , m_bindPoint(bindPoint)
        , m_generation(generation) {}

    void setBytes(std::string_view name, const void* data, size_t size);

    CommandEncoder* m_encoder;
    ref<ShaderProgram> m_program;
    VkPipelineBindPoint m_bindPoint;
    u64 m_generation;
  };
}  // namespace ren
