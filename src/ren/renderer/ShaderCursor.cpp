#include "./ShaderCursor.h"
#include <ren/renderer/Renderer.h>

namespace ren {



  ShaderCursor ShaderCursor::field(const char* name) {
    return field(m_typeLayout->findFieldIndexByName(name));
  }

  ShaderCursor ShaderCursor::field(int index) {
    slang::VariableLayoutReflection* field = m_typeLayout->getFieldByIndex(index);

    // Copy *this*
    ShaderCursor result = *this;
    // And update it with the new info.
    result.m_typeLayout = field->getTypeLayout();
    result.m_byteOffset += field->getOffset();
    result.m_bindingIndex += field->getOffset(slang::ParameterCategory::DescriptorTableSlot);

    return result;
  }

  ShaderCursor ShaderCursor::element(int index) {
    // Navigating a shader cursor to an array element is only slightly more
    // complicated than navigating to a structure field:
    slang::TypeLayoutReflection* elementTypeLayout = m_typeLayout->getElementTypeLayout();

    ShaderCursor result = *this;
    result.m_typeLayout = elementTypeLayout;
    result.m_byteOffset += index * elementTypeLayout->getStride();

    result.m_bindingArrayElement *= m_typeLayout->getElementCount();
    result.m_bindingArrayElement += index;

    return result;
  }


  void ShaderCursor::writeData(void const* data, size_t size) {
    // TODO: don't do this mapping so often!
    if (size > 512) {
      this->m_bufferData = (u8*)this->m_buffer->map();
      std::memcpy(this->m_bufferData + this->m_byteOffset, data, size);
      this->m_buffer->unmap();
      this->m_bufferData = nullptr;
    } else {
      vkCmdUpdateBuffer(m_cmd, this->m_buffer->getHandle(), m_byteOffset, size, data);
    }
  }



  void ShaderCursor::write(ref<Image> image, const Sampler& sampler) {
    VkDescriptorImageInfo imageInfo;
    imageInfo.imageView = image->getImageView();
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, 0};
    write.dstSet = m_descriptorSet;
    write.dstBinding = m_bindingIndex;
    write.dstArrayElement = m_bindingArrayElement;
    write.descriptorCount = 1;
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

    VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, 0};
    write.dstSet = m_descriptorSet;
    write.dstBinding = m_bindingIndex;
    write.dstArrayElement = m_bindingArrayElement;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(ren::getVulkan().device, 1, &write, 0, nullptr);
  }
}  // namespace ren