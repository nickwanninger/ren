#include <ren/renderer/Vulkan.h>

ren::Buffer::Buffer(VulkanInstance &vulkan_instance, VkDeviceSize size, VkBufferUsageFlags usage,
                    VkMemoryPropertyFlags properties)

    : vulkan(vulkan_instance)
    , size(size)
    , usage(usage)
    , properties(properties) {
  // Allocate the buffer using vma

  switch (usage) {
    case VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT: setName("UniformBuffer"); break;
    case VK_BUFFER_USAGE_STORAGE_BUFFER_BIT: setName("StorageBuffer"); break;
    case VK_BUFFER_USAGE_VERTEX_BUFFER_BIT: setName("VertexBuffer"); break;
    case VK_BUFFER_USAGE_INDEX_BUFFER_BIT: setName("IndexBuffer"); break;
    default: setName("GenericBuffer"); break;
  }

  resize(size);
}

ren::Buffer::~Buffer() {
  unmap();

  vmaDestroyBuffer(vulkan.allocator, buffer, allocation);
}

void ren::Buffer::resize(size_t new_bytes) {
  if (isMapped()) { throw std::runtime_error("Cannot resize a mapped buffer"); }

  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo allocInfo = {};
  allocInfo.preferredFlags = properties;
  allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
  allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT |
                    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;


  vmaCreateBuffer(vulkan.allocator, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr);
}


void *ren::Buffer::map(void) {
  if (this->mapped == nullptr) {
    // Map the memory
    vmaMapMemory(vulkan.allocator, this->allocation, &this->mapped);
  }
  return this->mapped;
}


void ren::Buffer::unmap(void) {
  if (this->mapped != nullptr) {
    // Unmap the memory
    vmaUnmapMemory(vulkan.allocator, this->allocation);
    this->mapped = nullptr;
  }
}


void ren::Buffer::copyFrom(const Buffer &src, VkDeviceSize size, VkDeviceSize srcOffset,
                           VkDeviceSize dstOffset) {
  this->vulkan.copy_buffer(src.buffer, this->buffer, size);
}


void ren::Buffer::copyFromHost(const void *data, VkDeviceSize size, VkDeviceSize offset) {
  // Ensure the size is within bounds
  if (offset + size > this->size) { throw std::runtime_error("Buffer copy exceeds buffer size"); }

  // Map the buffer and copy the data
  void *mappedData = this->map();
  std::memcpy(static_cast<u8 *>(mappedData) + offset, data, size);
  this->unmap();
}


void ren::Buffer::setName(const std::string &new_name) {
  name = new_name;
  // if (buffer != VK_NULL_HANDLE) {
  //   // Set the name of the buffer in Vulkan
  //   VkDebugUtilsObjectNameInfoEXT name_info{};
  //   name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
  //   name_info.objectType = VK_OBJECT_TYPE_BUFFER;
  //   name_info.objectHandle = reinterpret_cast<uint64_t>(buffer);
  //   name_info.pObjectName = name.c_str();
  //   vkSetDebugUtilsObjectNameEXT(vulkan.device, &name_info);
  // }
}
