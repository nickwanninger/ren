#include "./ParameterBinding.h"
#include <ren/renderer/Renderer.h>
#include "ren/renderer/Swapchain.h"

namespace ren {



  ParameterCursor::ParameterCursor(ref<ParameterBlock> block, ren::ShaderReflection::Node &node)
      : block(block)
      , node(node) {
    ren::println(" walk node '{}' ({})", node.name, node.location.toJson().dump());
  }


  ParameterCursor ParameterCursor::element(int index) {
    if (index < 0 || index >= static_cast<int>(node.members.size())) {
      throw std::runtime_error(fmt::format("ShaderCursor: element index '{}' out of bounds in node '{}'", index, node.name));
    }
    return ParameterCursor(block, *node.members[index]);
  }


  ParameterCursor ParameterCursor::field(const char *name) {
    // TODO: index?
    for (auto *child : node.members) {
      if (child->name == name) {
        return ParameterCursor(block, *child);
      }
    }

    // if we got here, throw.
    // TODO: exceptions are smelly.
    throw std::runtime_error(fmt::format("ShaderCursor: element '{}' not found in node '{}'", name, node.name));
  }


  void ParameterCursor::writeBytes(const void *data, size_t size) {
    // The node tells us where we are, and how many bytes we *can* write. If it
    // doesn't have this information, default to 0 bytes.
    auto allowedBytes = node.location.byteSize.value_or(0);
    if (size > allowedBytes) {
      throw std::runtime_error(
          fmt::format("ShaderCursor: write of {} bytes exceeds allowed size of {} bytes in node '{}'", size, allowedBytes, node.name));
    }

    u32 offset = node.location.byteOffset.value_or(0);
    u32 bindingIndex = node.location.bindingIndex.value_or(0);

    // DEBUG:
    std::string dataStr;
    for (size_t i = 0; i < size; i++) {
      dataStr += fmt::format("{:02X} ", ((u8 *)data)[i]);
    }
    ren::println("write {} bytes to node '{}' at binding {}, offset {}: {}", size, node.name, bindingIndex, offset, dataStr);
  }


  ParameterCursor ShaderObject::block(const char *name) {
    auto refl = program->getReflection();
    auto root = refl->getRoot();


    ref<ParameterBlock> binding;

    ShaderReflection::Node *node = nullptr;

    // now try to find the node.
    for (auto *child : root->members) {
      if (child->name == name) {
        node = child;
      }
    }

    if (node == nullptr) {
      throw std::runtime_error(fmt::format("ShaderRoot: parameter block '{}' not found in program", name));
    }


    // Find a ParameterBlock for the node.
    auto it = bindings.find(node);
    if (it == bindings.end()) {
      auto set = node->location.bindingSet.value_or(0);
      auto layout = this->program->getDescriptorSetLayouts()[set];
      if (layout == VK_NULL_HANDLE) {
        throw std::runtime_error(fmt::format("ShaderRoot: no descriptor set layout for set {} (node '{}')", set, node->name));
      }

      // Create a new one.
      binding = make<ParameterBlock>(refl, *node, layout);
      ren::println("Creating ParameterBlock for node '{}'", node->name);
      bindings[node] = binding;
    }


    return ParameterCursor(binding, *node);
  }



  ParameterBlock::ParameterBlock(ref<ShaderReflection> refl, ren::ShaderReflection::Node &node, VkDescriptorSetLayout layout)
      : refl(refl)
      , node(node) {
    REN_ASSERT(layout != VK_NULL_HANDLE);
    // node must also be a ParameterBlock
    REN_ASSERT(node.type.type == ShaderReflection::Type::ParameterBlock);

    // ren::getFrameData().descriptorAllocator.allocate(&this->descriptorSet, layout);

    // REN_ASSERT(this->descriptorSet != VK_NULL_HANDLE);

    ren::println("Allocated ParameterBlock for node '{}' with descriptor set {}", node.name, (void *)this->descriptorSet);


    // Now, if the node has a byte size, we need to allcoate a uniform buffer for them and bind it to the index of the node.
    auto loc = node.location;
    if (loc.byteSize && *loc.byteSize > 0) {
      u32 bindingIndex = loc.bindingIndex.value_or(0);
      ren::println("ParameterBlock node '{}' has byte size {}, allocating uniform buffer for binding index {}", node.name, *loc.byteSize, bindingIndex);
    }
  }


}  // namespace ren