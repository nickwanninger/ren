#include "./ShaderCursor.h"
#include <ren/renderer/Renderer.h>

namespace ren {



  ShaderCursor ShaderCursor::field(const char* name) {
    // TODO: index?
    for (auto& member : refl->members) {
      if (member->name == name) {
        ShaderCursor result = *this;
        result.refl = member;
        return result;
      }
    }
    throw std::runtime_error("ShaderCursor::field: field not found: " + std::string(name));
  }

  ShaderCursor ShaderCursor::field(u32 index) {
    ShaderCursor result = *this;
    result.refl = refl->members[index];
    return result;
  }

  ShaderCursor ShaderCursor::element(u32 index) { return field(index); }


  void ShaderCursor::writeData(void const* data, size_t size) {
    // TODO: figure all this out!
    // // TODO: don't do this mapping so often!
    // if (size > 512) {
    //   this->m_bufferData = (u8*)this->m_buffer->map();
    //   std::memcpy(this->m_bufferData + this->m_byteOffset, data, size);
    //   this->m_buffer->unmap();
    //   this->m_bufferData = nullptr;
    // } else {
    //   vkCmdUpdateBuffer(m_cmd, this->m_buffer->getHandle(), m_byteOffset, size, data);
    // }
  }



  void ShaderCursor::write(ref<Image> image, const Sampler& sampler) {
    VkDescriptorImageInfo imageInfo;
    imageInfo.imageView = image->getImageView();
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write = createWrite();
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(ren::getVulkan().device, 1, &write, 0, nullptr);
  }



  void ShaderCursor::write(ref<Image> image, VkFilter samplerFilter) {
    auto& R = ren::Renderer::get();
    return write(image, R.getSampler(samplerFilter));
  }


  void ShaderCursor::write(ref<Buffer> buffer) {
    VkDescriptorBufferInfo bufferInfo;
    bufferInfo.buffer = buffer->getHandle();
    bufferInfo.offset = 0;
    bufferInfo.range = buffer->getSize();

    VkWriteDescriptorSet write = createWrite();
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(ren::getVulkan().device, 1, &write, 0, nullptr);
  }


  VkWriteDescriptorSet ShaderCursor::createWrite() {
    VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, 0};


    auto bindingSet = refl->location.bindingSet.value();
    auto bindingIndex = refl->location.bindingIndex.value();

    write.dstSet = obj.getDescriptorSets()[bindingSet];
    write.dstBinding = bindingIndex;
    write.dstArrayElement = refl->location.arrayIndex.value_or(0);
    write.descriptorCount = 1;
    return write;
  }
}  // namespace ren