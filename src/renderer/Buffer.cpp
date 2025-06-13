#include <ren/Vulkan.h>

ren::Buffer::Buffer(VulkanInstance &vulkan_instance, VkDeviceSize size, VkBufferUsageFlags usage,
                    VkMemoryPropertyFlags properties)

    : vulkan(vulkan_instance)
    , size(size)
    , usage(usage)
    , properties(properties) {
  // Allocate the buffer using vma

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

ren::Buffer::~Buffer() {
  unmap();

  vmaDestroyBuffer(vulkan.allocator, buffer, allocation);
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
