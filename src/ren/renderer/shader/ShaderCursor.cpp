#include <ren/renderer/shader/ShaderCursor.h>

#include <ren/renderer/CommandEncoder.h>

namespace ren {
  void ShaderCursor::setBytes(
      std::string_view name, const void* data, size_t size) {
    m_encoder->writePushConstant(*this, name, data, size);
  }
}  // namespace ren
