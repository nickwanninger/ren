#include <ren/renderer/shader/ShaderCursor.h>

#include <ren/renderer/CommandEncoder.h>

namespace ren {
  ShaderCursor ShaderCursor::get(std::string_view name) const {
    if (m_node == nullptr) {
      throw std::runtime_error("ShaderCursor has no reflection node");
    }
    for (const auto* child : m_node->members) {
      if (child != nullptr && child->name == name) {
        return ShaderCursor(*this, *child);
      }
    }
    std::string available;
    for (const auto* child : m_node->members) {
      if (child == nullptr) {
        continue;
      }
      if (!available.empty()) {
        available += ", ";
      }
      available += child->name.empty() ? "<unnamed>" : child->name;
    }
    throw std::runtime_error(fmt::format(
        "ShaderCursor: field '{}' was not found in '{}' (available: {})",
        name, m_node->name.empty() ? "$root" : m_node->name, available));
  }

  ShaderCursor ShaderCursor::element(size_t index) const {
    if (m_node == nullptr || index >= m_node->members.size() ||
        m_node->members[index] == nullptr) {
      throw std::runtime_error(fmt::format(
          "ShaderCursor: element {} is out of bounds in '{}'",
          index, m_node == nullptr || m_node->name.empty()
                     ? "$root"
                     : m_node->name));
    }
    return ShaderCursor(*this, *m_node->members[index]);
  }

  ShaderCursor ShaderCursor::pushConstant(std::string_view name) const {
    const auto* root = m_program->getReflection()->getRoot();
    if (m_node != root) {
      throw std::runtime_error(
          "ShaderCursor::pushConstant() must be called on the root cursor");
    }
    auto cursor = get(name);
    if (cursor.m_node->type.type != ShaderReflection::Type::PushConstant) {
      throw std::runtime_error(fmt::format(
          "ShaderCursor: top-level field '{}' is {}, not a push constant",
          name, cursor.m_node->type.toString()));
    }
    return cursor;
  }

  void ShaderCursor::setBytes(const void* data, size_t size) {
    if (m_node == nullptr || !m_node->location.pushConstant) {
      throw std::runtime_error(fmt::format(
          "ShaderCursor: '{}' is not writable push-constant data",
          m_node == nullptr || m_node->name.empty() ? "$root" : m_node->name));
    }
    if (!m_node->location.byteOffset || !m_node->location.byteSize) {
      throw std::runtime_error(fmt::format(
          "ShaderCursor: '{}' has no reflected byte location",
          m_node->name));
    }

    const auto reflectedSize = *m_node->location.byteSize;
    if (size != reflectedSize) {
      throw std::runtime_error(fmt::format(
          "ShaderCursor: '{}' is {} bytes, but {} bytes were provided",
          m_node->name, reflectedSize, size));
    }

    m_encoder->writePushConstant(
        *this, *m_node->location.byteOffset, data, size);
  }
}  // namespace ren
