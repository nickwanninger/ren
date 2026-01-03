#include "./ParameterBinding.h"
#include <ren/renderer/Renderer.h>

namespace ren {



  ParameterCursor::ParameterCursor(ren::ShaderReflection::Node &node)
      : node(node) {
    ren::println(" walk node '{}' ({})", node.name,
                 node.location.toJson().dump());
  }


  ParameterCursor ParameterCursor::element(int index) {
    if (index < 0 || index >= static_cast<int>(node.members.size())) {
      throw std::runtime_error(fmt::format(
          "ShaderCursor: element index '{}' out of bounds in node '{}'", index, node.name));
    }
    return ParameterCursor(*node.members[index]);
  }


  ParameterCursor ParameterCursor::field(const char *name) {
    // TODO: index?
    for (auto *child : node.members) {
      if (child->name == name) return ParameterCursor(*child);
    }

    // if we got here, throw.
    // TODO: exceptions are smelly.
    throw std::runtime_error(
        fmt::format("ShaderCursor: element '{}' not found in node '{}'", name, node.name));
  }


  void ParameterCursor::writeBytes(const void *data, size_t size) {
    // The node tells us where we are, and how many bytes we *can* write. If it
    // doesn't have this information, default to 0 bytes.
    auto allowedBytes = node.location.byteSize.value_or(0);
    if (size > allowedBytes) {
      throw std::runtime_error(fmt::format(
          "ShaderCursor: write of {} bytes exceeds allowed size of {} bytes in node '{}'", size,
          allowedBytes, node.name));
    }

    u32 offset = node.location.byteOffset.value_or(0);
    u32 bindingIndex = node.location.bindingIndex.value_or(0);

    // DEBUG:
    std::string dataStr;
    for (size_t i = 0; i < size; i++) {
      dataStr += fmt::format("{:02X} ", ((u8 *)data)[i]);
    }
    ren::println("write {} bytes to node '{}' at binding {}, offset {}: {}", size, node.name,
                 bindingIndex, offset, dataStr);
  }


  ParameterCursor ShaderObject::block(const char *name) {
    auto refl = program->getReflection();
    auto root = refl->getRoot();


    ref<ParameterBlock> binding;

    ShaderReflection::Node *node = nullptr;

    // now try to find the node.
    for (auto *child : root->members) {
      if (child->name == name) { node = child; }
    }

    if (node == nullptr) {
      throw std::runtime_error(
          fmt::format("ShaderRoot: parameter block '{}' not found in program", name));
    }


    // Find a ParameterBlock for the node.
    auto it = bindings.find(node);
    if (it == bindings.end()) {
      // Create a new one.
      binding = make<ParameterBlock>();
      ren::println("Creating ParameterBlock for node '{}'", node->name);
      bindings[node] = binding;
    }


    return ParameterCursor(*node);
  }


}  // namespace ren