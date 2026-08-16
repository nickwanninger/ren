#include <ren/renderer/Buffer.h>
#include <ren/renderer/vulkan/Vulkan.h>
#include <ren/core/Instrumentation.h>
#include <ren/renderer/submission/SubmissionQueue.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include "json/json.hpp"


namespace ren {


  static VkMemoryPropertyFlags memoryPropertyFlagsForDomain(BufferDomain domain) {
    switch (domain) {
      case BufferDomain::Device:
        return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
      case BufferDomain::Upload:
        return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
      case BufferDomain::Readback:
        return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    }
    throw std::runtime_error("Unknown buffer memory domain");
  }

  static VmaAllocationCreateFlags vmaAllocationFlagsForDomain(BufferDomain domain) {
    switch (domain) {
      case BufferDomain::Device:
        return 0;
      case BufferDomain::Upload:
        return VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
      case BufferDomain::Readback:
        return VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    }
    throw std::runtime_error("Unknown buffer memory domain");
  }




  BufferMemory::BufferMemory(size_t byteCount, BufferDomain domain, VkBufferUsageFlags usage)
      : BufferMemory(byteCount, usage, memoryPropertyFlagsForDomain(domain), vmaAllocationFlagsForDomain(domain)) {}


  BufferMemory::BufferMemory(size_t byteCount, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VmaAllocationCreateFlags vmaFlags)
      : usage(usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
      , properties(properties)
      , vmaFlags(vmaFlags) {
    resizeBytes(byteCount);
  }

  void BufferMemory::resizeBytes(size_t newByteCount) {
    if (newByteCount == byteCount && (newByteCount == 0 || buffer != VK_NULL_HANDLE)) {
      return;
    }

    auto &vulkan = getVulkan();
    VkBuffer newBuffer = VK_NULL_HANDLE;
    VmaAllocation newAllocation = VK_NULL_HANDLE;
    void *newHostAddress = nullptr;
    uintptr_t newGpuAddress = 0;

    auto destroyNewBuffer = [&] {
      if (newHostAddress != nullptr) {
        vmaUnmapMemory(vulkan.allocator, newAllocation);
        newHostAddress = nullptr;
      }
      if (newBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(vulkan.allocator, newBuffer, newAllocation);
        newBuffer = VK_NULL_HANDLE;
        newAllocation = VK_NULL_HANDLE;
      }
    };

    if (newByteCount != 0) {
      VkBufferCreateInfo bufferInfo{};
      bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
      bufferInfo.size = newByteCount;
      bufferInfo.usage = usage;
      bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

      VmaAllocationCreateInfo allocInfo{};
      allocInfo.preferredFlags = properties;
      allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
      allocInfo.flags = vmaFlags;

      VkResult result = vmaCreateBuffer(vulkan.allocator, &bufferInfo, &allocInfo, &newBuffer, &newAllocation, nullptr);
      if (result != VK_SUCCESS) {
        destroyNewBuffer();
        throw std::runtime_error(fmt::format("Failed to create buffer: size={}, usage={:#x}, properties={:#x}, VkResult={}", newByteCount, usage,
                                             properties, static_cast<int>(result)));
      }

      VkBufferDeviceAddressInfo addrInfo{};
      addrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
      addrInfo.buffer = newBuffer;
      newGpuAddress = vkGetBufferDeviceAddress(vulkan.device, &addrInfo);

      if (!(properties & static_cast<VkMemoryPropertyFlags>(MemoryProperty::DeviceLocal))) {
        result = vmaMapMemory(vulkan.allocator, newAllocation, &newHostAddress);
        if (result != VK_SUCCESS) {
          destroyNewBuffer();
          throw std::runtime_error(fmt::format("Failed to map buffer memory: VkResult={}", static_cast<int>(result)));
        }
      }
    }

    try {
      const size_t copySize = std::min(byteCount, newByteCount);
      if (buffer != VK_NULL_HANDLE && newBuffer != VK_NULL_HANDLE && copySize != 0) {
        auto cmd = vulkan.beginSingleTimeCommands();
        VkBufferCopy copyRegion{};
        copyRegion.size = copySize;
        vkCmdCopyBuffer(cmd, buffer, newBuffer, 1, &copyRegion);
        vulkan.endSingleTimeCommands(cmd);
      }
    } catch (...) {
      destroyNewBuffer();
      throw;
    }

    if (hostAddress != nullptr) {
      vmaUnmapMemory(vulkan.allocator, allocation);
    }
    if (buffer != VK_NULL_HANDLE) {
      vmaDestroyBuffer(vulkan.allocator, buffer, allocation);
    }

    byteCount = newByteCount;
    gpuAddress = newGpuAddress;
    hostAddress = newHostAddress;
    allocation = newAllocation;
    buffer = newBuffer;
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
      throw std::runtime_error("BufferMemory copy exceeds buffer size");
    }
    std::memcpy(static_cast<u8 *>(hostAddress) + offset, data, size);
  }



}  // namespace ren
