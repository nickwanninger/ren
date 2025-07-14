#include <ren/renderer/ShaderBinder.h>
#include <ren/renderer/Renderer.h>


namespace ren {

  ShaderBinder::ShaderBinder(ShaderProgram &program, u32 set)
      : set(set)
      , program(program) {
    u32 bindingsInSet = 0;
    // Count how many image bindings we have in this set.
    for (const auto &binding : program.getBindings()) {
      if (binding.set == set) { bindingsInSet++; }
    }
    // preallocate the imageinfos vector to avoid pointer invalidation.
    this->imageInfos.reserve(bindingsInSet);
    this->bufferInfos.reserve(bindingsInSet);
  }


  void ShaderBinder::bind(const std::string_view &name, const ren::Buffer &buffer) {
    // Find the binding for this name in the shader program.
    const auto *binding = program.getBinding(name);
    if (binding == nullptr) {
      throw std::runtime_error(
          fmt::format("Shader binding '{}' not found in program '{}'", name, json(program).dump()));
    }

    // fmt::println("Binding UBS '{}' to shader binding '{}' ({}.{})", UBS.getName(), name,
    //              binding->set, binding->binding);

    VkWriteDescriptorSet newWrite{};
    newWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    newWrite.pNext = nullptr;

    // Create the image info for this texture.
    bufferInfos.emplace_back();
    VkDescriptorBufferInfo *bufferInfo = &bufferInfos.back();
    bufferInfo->buffer = buffer.getHandle();
    bufferInfo->offset = 0;  // TODO: support offsets.
    bufferInfo->range = buffer.getSize();

    newWrite.descriptorCount = 1;  // TODO: support arrays of buffers?
    newWrite.descriptorType = binding->type;
    newWrite.pBufferInfo = bufferInfo;
    newWrite.dstBinding = binding->binding;

    writes.push_back(newWrite);
  }

  void ShaderBinder::bind(const std::string_view &name, const Texture &texture) {
    // Find the binding for this name in the shader program.
    const auto *binding = program.getBinding(name);
    if (binding == nullptr) {
      throw std::runtime_error(
          fmt::format("Shader binding '{}' not found in program '{}'", name, json(program).dump()));
    }

    // fmt::println("Binding texture '{}' to shader binding '{}' ({}.{})", texture.getName(), name,
    //              binding->set, binding->binding);

    VkWriteDescriptorSet newWrite{};
    newWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    newWrite.pNext = nullptr;

    // Create the image info for this texture.
    imageInfos.emplace_back();
    VkDescriptorImageInfo *imageInfo = &imageInfos.back();
    imageInfo->sampler = texture.getSampler();
    imageInfo->imageView = texture.getImageView();
    imageInfo->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    newWrite.descriptorCount = 1;  // TODO: support arrays of textures.
    newWrite.descriptorType = binding->type;
    newWrite.pImageInfo = imageInfo;
    newWrite.dstBinding = binding->binding;

    writes.push_back(newWrite);
  }


  void ShaderBinder::bind(const std::string_view &name, const Image &image, Sampler &sampler) {
    // Find the binding for this name in the shader program.
    const auto *binding = program.getBinding(name);
    if (binding == nullptr) {
      throw std::runtime_error(
          fmt::format("Shader binding '{}' not found in program '{}'", name, json(program).dump()));
    }

    VkWriteDescriptorSet newWrite{};
    newWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    newWrite.pNext = nullptr;

    // Create the image info for this texture.
    imageInfos.emplace_back();
    VkDescriptorImageInfo *imageInfo = &imageInfos.back();
    imageInfo->sampler = sampler.getHandle();
    imageInfo->imageView = image.getImageView();
    imageInfo->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    newWrite.descriptorCount = 1;  // TODO: support arrays of textures.
    newWrite.descriptorType = binding->type;
    newWrite.pImageInfo = imageInfo;
    newWrite.dstBinding = binding->binding;

    writes.push_back(newWrite);
  }

  void ShaderBinder::bind(const std::string_view &name, const Image &image,
                          VkFilter samplerFilter) {
    auto &R = ren::Renderer::get();
    bind(name, image, R.getSampler(samplerFilter));
  }


  void ShaderBinder::apply(void) {
    auto &frame = getFrameData();
    // build the descriptor sets and write them.
    auto layout = program.getDescriptorSetLayouts()[set];
    VkDescriptorSet descriptorSet;
    if (!frame.descriptorAllocator.allocate(&descriptorSet, layout)) {
      throw std::runtime_error(fmt::format("Could not apply shader binder for set {} in {}",
                                           this->set, json(program).dump()));
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