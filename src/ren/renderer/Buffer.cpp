#include <ren/renderer/Buffer.h>
#include <ren/renderer/vulkan/Vulkan.h>
#include <ren/core/Instrumentation.h>
#include <ren/renderer/submission/SubmissionQueue.h>
#include <stdexcept>
#include "json/json.hpp"


namespace ren {


  BufferMemory::BufferMemory(size_t byteCount, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VmaAllocationCreateFlags vmaFlags)
      : byteCount(byteCount)
      , usage(usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
      , properties(properties)
      , vmaFlags(vmaFlags) {
    // Setup Vulkan buffer creation
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = byteCount;
    bufferInfo.usage = this->usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // Setup VMA allocation
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.preferredFlags = properties;
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = vmaFlags;

    // Allocate buffer
    VkResult result = vmaCreateBuffer(getVulkan().allocator, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr);

    if (result != VK_SUCCESS) {
      throw std::runtime_error(fmt::format("Failed to create buffer: size={}, usage={:#x}, properties={:#x}, VkResult={}", byteCount, usage,
                                           properties, static_cast<int>(result)));
    }

    // Query device address
    VkBufferDeviceAddressInfo addrInfo{};
    addrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addrInfo.buffer = buffer;
    gpuAddress = vkGetBufferDeviceAddress(getVulkan().device, &addrInfo);

    // Persistent mapping if requested
    if (properties & static_cast<VkMemoryPropertyFlags>(MemoryProperty::DeviceLocal)) {
      hostAddress = nullptr;
    } else {
      result = vmaMapMemory(getVulkan().allocator, allocation, &hostAddress);
      if (result != VK_SUCCESS) {
        printf("Couldn't map buffer memory: VkResult=%d\n", static_cast<int>(result));
      }
    }
  }

  BufferMemory::~BufferMemory() {
    // Unmap if mapped
    if (hostAddress != nullptr) {
      vmaUnmapMemory(getVulkan().allocator, allocation);
      hostAddress = nullptr;
    }

    // Destroy buffer if valid
    if (buffer != VK_NULL_HANDLE) {
      vmaDestroyBuffer(getVulkan().allocator, buffer, allocation);
      buffer = VK_NULL_HANDLE;
      allocation = VK_NULL_HANDLE;
    }
  }

  BufferMemory::BufferMemory(BufferMemory &&other) noexcept
      : byteCount(other.byteCount)
      , usage(other.usage)
      , properties(other.properties)
      , vmaFlags(other.vmaFlags)
      , gpuAddress(other.gpuAddress)
      , hostAddress(other.hostAddress)
      , allocation(other.allocation)
      , buffer(other.buffer) {
    // Zero out source
    other.byteCount = 0;
    other.usage = 0;
    other.properties = 0;
    other.vmaFlags = 0;
    other.gpuAddress = 0;
    other.hostAddress = nullptr;
    other.allocation = VK_NULL_HANDLE;
    other.buffer = VK_NULL_HANDLE;
  }

  BufferMemory &BufferMemory::operator=(BufferMemory &&other) noexcept {
    if (this != &other) {
      // Clean up existing resources (destructor logic)
      if (hostAddress != nullptr) {
        vmaUnmapMemory(getVulkan().allocator, allocation);
      }
      if (buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(getVulkan().allocator, buffer, allocation);
      }

      // Move from other
      byteCount = other.byteCount;
      usage = other.usage;
      properties = other.properties;
      vmaFlags = other.vmaFlags;
      gpuAddress = other.gpuAddress;
      hostAddress = other.hostAddress;
      allocation = other.allocation;
      buffer = other.buffer;

      // Zero out source
      other.byteCount = 0;
      other.usage = 0;
      other.properties = 0;
      other.vmaFlags = 0;
      other.gpuAddress = 0;
      other.hostAddress = nullptr;
      other.allocation = VK_NULL_HANDLE;
      other.buffer = VK_NULL_HANDLE;
    }
    return *this;
  }

  void BufferMemory::copyFromHost(const void *data, size_t size, size_t offset) {
    if (hostAddress == nullptr) {
      throw std::runtime_error("Cannot copy host data into a device-only buffer");
    }
    if (offset + size > byteCount) {
      throw std::runtime_error("BufferMemory copy exceeds immutable allocation size");
    }
    std::memcpy(static_cast<u8 *>(hostAddress) + offset, data, size);
  }

  BufferMemory allocateBuffer(
      size_t byteCount, BufferDomain domain, VkBufferUsageFlags usage) {
    switch (domain) {
      case BufferDomain::Device:
        return BufferMemory(
            byteCount, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      case BufferDomain::Upload:
        return BufferMemory(
            byteCount, usage,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
      case BufferDomain::Readback:
        return BufferMemory(
            byteCount, usage,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);
    }
    throw std::runtime_error("Unknown buffer memory domain");
  }


  Buffer::Buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties)
      : usage(usage)
      , properties(properties) {
    REN_PROFILE_FUNCTION();

    // Allocate the buffer using vma
    switch (usage) {
      case VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT:
        setName("UniformBuffer");
        break;
      case VK_BUFFER_USAGE_STORAGE_BUFFER_BIT:
        setName("StorageBuffer");
        break;
      case VK_BUFFER_USAGE_VERTEX_BUFFER_BIT:
        setName("VertexBuffer");
        break;
      case VK_BUFFER_USAGE_INDEX_BUFFER_BIT:
        setName("IndexBuffer");
        break;
      default:
        setName("GenericBuffer");
        break;
    }

    resizeBytes(size);
  }

  Buffer::~Buffer() {
    unmap();

    vmaDestroyBuffer(getVulkan().allocator, buffer, allocation);
  }

  void Buffer::resizeBytes(size_t newSize) {
    if (isMapped()) {
      throw std::runtime_error("Cannot resize a mapped buffer");
    }

    auto oldBuffer = buffer;
    auto oldAllocation = allocation;
    auto oldSize = size;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = newSize;
    bufferInfo.usage = usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.preferredFlags = properties;
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    vmaCreateBuffer(getVulkan().allocator, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr);
    this->size = newSize;



    // Get the device address
    // VkBufferDeviceAddressInfo addressInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = buffer};
    // VkDeviceAddress address = vkGetBufferDeviceAddress(getVulkan().device, &addressInfo);
    // println("New buffer address: 0x{:x}", address);

    // copy data from the old buffer to the new buffer if it exists.
    if (oldBuffer != VK_NULL_HANDLE) {
      auto copySize = oldSize;
      if (newSize < copySize) {
        copySize = newSize;
      }
      dbgln("Resizing buffer from {} to {}, copying {} bytes", oldSize, newSize, copySize);

      auto &vk = getVulkan();
      auto cmd = vk.beginSingleTimeCommands();

      VkBufferCopy copyRegion{};
      copyRegion.srcOffset = 0;
      copyRegion.dstOffset = 0;
      copyRegion.size = copySize;
      vkCmdCopyBuffer(cmd, oldBuffer, buffer, 1, &copyRegion);

      vkEndCommandBuffer(cmd);
      vk.transferQueue->submitOne(cmd)->awaitCompletion();

      vmaDestroyBuffer(getVulkan().allocator, oldBuffer, oldAllocation);
    }
  }


  void *Buffer::map(void) {
    if (this->mapped == nullptr) {
      // Map the memory
      vmaMapMemory(getVulkan().allocator, this->allocation, &this->mapped);
    }
    return this->mapped;
  }


  void Buffer::unmap(void) {
    if (this->mapped != nullptr) {
      // Unmap the memory
      vmaUnmapMemory(getVulkan().allocator, this->allocation);
      this->mapped = nullptr;
    }
  }


  void Buffer::copyFrom(const Buffer &src, VkDeviceSize size, VkDeviceSize srcOffset, VkDeviceSize dstOffset) {
    auto &vk = getVulkan();
    auto cmd = vk.beginSingleTimeCommands();

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = srcOffset;
    copyRegion.dstOffset = dstOffset;
    copyRegion.size = size;
    vkCmdCopyBuffer(cmd, src.buffer, this->buffer, 1, &copyRegion);
    vkEndCommandBuffer(cmd);

    vk.transferQueue->submitOne(cmd)->awaitCompletion();
  }


  void Buffer::copyFromHost(const void *data, VkDeviceSize size, VkDeviceSize offset) {
    // Ensure the size is within bounds
    if (offset + size > this->size) {
      throw std::runtime_error("Buffer copy exceeds buffer size");
    }

    // Map the buffer and copy the data
    void *mappedData = this->map();
    std::memcpy(static_cast<u8 *>(mappedData) + offset, data, size);
    this->unmap();
  }


  void Buffer::setName(const std::string &new_name) {
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
}  // namespace ren
