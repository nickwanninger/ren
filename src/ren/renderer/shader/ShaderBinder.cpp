#include <ren/renderer/shader/ShaderBinder.h>
#include <ren/renderer/Renderer.h>
#include <fmt/format.h>


namespace ren {

  ShaderBinder::ShaderBinder(ShaderProgram &program, u32 set)
      : set(set)
      , program(program) {}


  ShaderBinder &ShaderBinder::bind(const ShaderBinding &binding, const ren::Buffer &buffer) {
    VkWriteDescriptorSet newWrite{};
    newWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    newWrite.pNext = nullptr;

    // Create the image info for this texture.
    VkDescriptorBufferInfo *bufferInfo = arena.push<VkDescriptorBufferInfo>();
    bufferInfo->buffer = buffer.getHandle();
    bufferInfo->offset = 0;  // TODO: support offsets.
    bufferInfo->range = buffer.getSize();

    newWrite.descriptorCount = 1;  // TODO: support arrays of buffers?
    newWrite.descriptorType = binding.type;
    newWrite.pBufferInfo = bufferInfo;
    newWrite.dstBinding = binding.binding;

    writes.push_back(newWrite);
    return *this;
  }

  ShaderBinder &ShaderBinder::bind(const ShaderBinding &binding, const Texture &texture) {
    VkWriteDescriptorSet newWrite{};
    newWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    newWrite.pNext = nullptr;

    // Create the image info for this texture.
    VkDescriptorImageInfo *imageInfo = arena.push<VkDescriptorImageInfo>();
    imageInfo->sampler = texture.getSampler();
    imageInfo->imageView = texture.getImageView();
    imageInfo->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    newWrite.descriptorCount = 1;  // TODO: support arrays of textures.
    newWrite.descriptorType = binding.type;
    newWrite.pImageInfo = imageInfo;
    newWrite.dstBinding = binding.binding;

    writes.push_back(newWrite);
    return *this;
  }


  ShaderBinder &ShaderBinder::bind(const ShaderBinding &binding,
                                   const std::span<ref<Texture>> &textures) {
    VkWriteDescriptorSet newWrite{};
    newWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    newWrite.pNext = nullptr;

    // Create the image infos for these textures.

    auto *infos = arena.pushArray<VkDescriptorImageInfo>(textures.size());
    for (size_t i = 0; i < textures.size(); i++) {
      infos[i].sampler = textures[i]->getSampler();
      infos[i].imageView = textures[i]->getImageView();
      infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    newWrite.descriptorCount = static_cast<u32>(textures.size());
    newWrite.descriptorType = binding.type;
    newWrite.pImageInfo = infos;
    newWrite.dstBinding = binding.binding;

    writes.push_back(newWrite);
    return *this;
  }


  ShaderBinder &ShaderBinder::bind(const ShaderBinding &binding, const Image &image,
                                   Sampler &sampler) {
    VkWriteDescriptorSet newWrite{};
    newWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    newWrite.pNext = nullptr;

    // Create the image info for this texture.
    VkDescriptorImageInfo *imageInfo = arena.push<VkDescriptorImageInfo>();
    imageInfo->sampler = sampler.getHandle();
    imageInfo->imageView = image.getImageView();
    imageInfo->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    newWrite.descriptorCount = 1;  // TODO: support arrays of textures.
    newWrite.descriptorType = binding.type;
    newWrite.pImageInfo = imageInfo;
    newWrite.dstBinding = binding.binding;

    writes.push_back(newWrite);
    return *this;
  }

  ShaderBinder &ShaderBinder::bind(const ShaderBinding &binding, const Image &image,
                                   VkFilter samplerFilter) {
    auto &R = ren::Renderer::get();
    return bind(binding, image, R.getSampler(samplerFilter));
  }

  void ShaderBinder::apply(void) {
    auto &frame = getFrameData();
    // build the descriptor sets and write them.
    const auto &layouts = program.getDescriptorSetLayouts();
    if (set >= layouts.size()) {
      throw std::runtime_error(
          fmt::format("Descriptor set {} not available in program ({} sets)", set, layouts.size()));
    }
    auto layout = layouts[set];
    if (layout == VK_NULL_HANDLE) {
      throw std::runtime_error(fmt::format(
          "Descriptor set {} is sparse/unavailable (no layout). Did you reflect that set?", set));
    }
    VkDescriptorSet descriptorSet;
    if (!frame.descriptorAllocator.allocate(&descriptorSet, layout)) {
      ren::errln("Could not allocate descriptor set for set {} in {}", this->set,
                   json(program).dump());
      return;
    }
    // now that we have a descriptor set, apply the writes.
    for (VkWriteDescriptorSet &w : writes) {
      w.dstSet = descriptorSet;
    }

    // Now we can update the descriptor sets with the writes.
    vkUpdateDescriptorSets(getVulkan().device, writes.size(), writes.data(), 0, nullptr);
    writes.clear();
    // And bind that set.
    vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            program.getPipelineLayout(), this->set, 1, &descriptorSet, 0, nullptr);
  }

}  // namespace ren
