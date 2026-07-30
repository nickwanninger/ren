#include "./ParameterBinding.h"
#include <ren/renderer/submission/SubmissionUnit.h>
#include <ren/renderer/CommandEncoder.h>
#include <ren/renderer/Renderer.h>

namespace ren {



  ParameterCursor::ParameterCursor(ParameterBlock &block, ren::ShaderReflection::Node &node)
      : block(block)
      , node(node) {
    // ren::println(" walk node '{}' ({})", node.name, node.location.toJson().dump());
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


  void ParameterCursor::setBytes(const void *data, size_t size) {
    // The node tells us where we are, and how many bytes we *can* write. If it
    // doesn't have this information, default to 0 bytes.
    auto allowedBytes = node.location.byteSize.value_or(0);
    if (size > allowedBytes) {
      throw std::runtime_error(
          fmt::format("ShaderCursor: write of {} bytes exceeds allowed size of {} bytes in node '{}'", size, allowedBytes, node.name));
    }

    u32 offset = node.location.byteOffset.value_or(0);

    block.writeUniform(offset, data, size);
  }

  void ParameterCursor::bind(ref<Buffer> buffer) {
    auto bindingIndex = node.location.bindingIndex.value_or(0);
    block.set(bindingIndex, buffer);
  }


  ParameterCursor ShaderObject::block(const char *name) {
    auto refl = program->getReflection();
    auto root = refl->getRoot();


    ParameterBlock *binding = nullptr;

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
      binding = unit.getArena().push<ParameterBlock>(this->unit, refl, *node, layout);
      // ren::println("Creating ParameterBlock for node '{}'", node->name);
      bindings[node] = binding;
    }


    return ParameterCursor(*binding, *node);
  }

  void ShaderObject::bind(VkCommandBuffer cmd, VkPipelineBindPoint bindPoint, VkPipelineLayout layout) {
    for (auto &[node, block] : bindings) {
      // Get the set index from the node location
      u32 setIndex = node->location.bindingSet.value_or(0);
      VkDescriptorSet set = block->descriptorSet;
      if (set != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, bindPoint, layout, setIndex, 1, &set, 0, nullptr);
      }
    }
  }



  ParameterBlock::ParameterBlock(SubmissionUnit &unit, ref<ShaderReflection> refl, ren::ShaderReflection::Node &node, VkDescriptorSetLayout layout)
      : refl(refl)
      , node(node)
      , unit(unit) {
    REN_ASSERT(layout != VK_NULL_HANDLE);
    // node must also be a ParameterBlock
    REN_ASSERT(node.type.type == ShaderReflection::Type::ParameterBlock);

    unit.getDescriptorAllocator().allocate(&this->descriptorSet, layout);

    REN_ASSERT(this->descriptorSet != VK_NULL_HANDLE);

    // ren::println("Allocated ParameterBlock for node '{}' with descriptor set {}", node.name, (void *)this->descriptorSet);


    // Now, if the node has a byte size, we need to allcoate a uniform buffer for them and bind it to the index of the node.
    auto loc = node.location;
    if (loc.byteSize && *loc.byteSize > 0) {
      u32 bindingIndex = loc.bindingIndex.value_or(0);
      // ren::println("ParameterBlock node '{}' has byte size {}, allocating uniform buffer for binding index {}", node.name, *loc.byteSize,
      // bindingIndex);

      // Allocate the implicit buffer
      this->implicitBuffer = ren::make<UniformBuffer<u8>>(*loc.byteSize);

      // Bind it to the descriptor set.
      this->set(bindingIndex, this->implicitBuffer);
    }
  }

  void ParameterBlock::writeUniform(u32 offset, const void *data, size_t size) {
    if (!this->implicitBuffer) {
      throw std::runtime_error(fmt::format("ParameterBlock: attempt to write to implicit uniform buffer on block '{}' which has none", node.name));
    }

    // If the size is smaller than 128 bytes, use vkCmdUpdateBuffer
    if (size < 128) {
      auto cmd = unit.getMainCommandEncoder()->buf();
      vkCmdUpdateBuffer(cmd, this->implicitBuffer->getHandle(), offset, size, data);
    } else {
      this->implicitBuffer->copyFromHost(data, size, offset);
    }
  }

  void ParameterBlock::set(u32 binding, ref<Buffer> buffer) {
    // Write the descriptor set
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = buffer->getHandle();
    bufferInfo.offset = 0;
    bufferInfo.range = buffer->getSize();

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = this->descriptorSet;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;  // TODO: CHECK THIS!
    // If it's the implicit buffer, it's uniform
    if (buffer == this->implicitBuffer) {
      write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    }
    write.descriptorCount = 1;
    write.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(ren::getVulkan().device, 1, &write, 0, nullptr);

    // Track the resource
    this->boundResources[binding] = buffer;
  }


}  // namespace ren
