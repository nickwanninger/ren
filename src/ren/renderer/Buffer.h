#pragma once

#include <ren/types.h>
#include <ren/core/Instrumentation.h>
#include <vulkan/vulkan_core.h>
#include <vector>
#include <ren/core/Builder.h>
#include <string>
#include "glm/gtc/constants.hpp"
#include "ren/renderer/vulkan/Vulkan.h"

namespace ren {
  class VulkanInstance;
  u32 getFrameIndex(void);




  // Nicer names than the vulkan ones...
  enum class MemoryUsage : VkBufferUsageFlags {
    Uniform = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
    Storage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    Vertex = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
    Index = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
    Indirect = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,

    Transfer = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
  };
  REN_FLAG_ENUM(MemoryUsage, VkBufferUsageFlags)

  enum class MemoryProperty : u32 {
    DeviceLocal = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    HostVisible = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
    HostCoherent = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    HostCached = VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
    LazilyAllocated = VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT,
  };
  REN_FLAG_ENUM(MemoryProperty, u32)

  enum class BufferDomain {
    Device,
    Upload,
    Readback,
  };
  REN_FLAG_ENUM(BufferDomain, u32)


  // This is the owning class for a Vulkan buffer. It manages the VMA
  // allocation, the VkBuffer handle, and the device address. It also provides a
  // host pointer for copying into it.  It is rare that you will need to use
  // this class directly. Instead, use the wrappers/subclasses after it.
  class BufferMemory : public RefCounted<BufferMemory> {
   public:
    // Higher level constructor
    BufferMemory(size_t byteCount, BufferDomain domain = BufferDomain::Device, VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    // low level constructor
    BufferMemory(size_t byteCount, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VmaAllocationCreateFlags vmaFlags = 0);

    // Destructor - unmaps (if mapped) and destroys VMA allocation
    ~BufferMemory();

    // Non-copyable (owns GPU resource)
    BufferMemory(const BufferMemory &) = delete;
    BufferMemory &operator=(const BufferMemory &) = delete;

    // Movable (transfers ownership)
    BufferMemory(BufferMemory &&other) noexcept;
    BufferMemory &operator=(BufferMemory &&other) noexcept;

    // Public interface
    VkBuffer getHandle() const { return buffer; }
    uintptr_t getGPUAddress() const { return gpuAddress; }
    void *getHostAddress() const { return hostAddress; }
    size_t getByteCount() const { return byteCount; }
    bool isMapped() const { return hostAddress != nullptr; }
    bool isValid() const { return buffer != VK_NULL_HANDLE; }

    template <typename T>
    T *hostData() const {
      return static_cast<T *>(hostAddress);
    }

    template <typename T>
    VkDeviceAddress devicePointer(size_t elementOffset = 0) const {
      static_assert(std::is_trivially_copyable_v<T>);
      return static_cast<VkDeviceAddress>(gpuAddress + elementOffset * sizeof(T));
    }

    void copyFromHost(const void *data, size_t size, size_t offset = 0);

    // Reallocate while preserving the first min(oldSize, newByteCount) bytes; zero releases the allocation.
    void resizeBytes(size_t newByteCount);

   private:
    // Constructor parameters
    size_t byteCount = 0;
    VkBufferUsageFlags usage = 0;
    VkMemoryPropertyFlags properties = 0;
    VmaAllocationCreateFlags vmaFlags = 0;

    // Runtime state
    uintptr_t gpuAddress = 0;
    void *hostAddress = nullptr;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
  };

  template <typename T>
  class TypedBuffer : public BufferMemory {
   public:
    using EntryType = T;
    TypedBuffer(size_t count, BufferDomain domain, VkBufferUsageFlags usage)
        : BufferMemory(count * sizeof(T), domain, usage) {}
    TypedBuffer(size_t count, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties)
        : BufferMemory(count * sizeof(T), usage, properties) {}

    virtual ~TypedBuffer() = default;

    // Wrap the map function into the typed version.
    T *hostData(void) { return BufferMemory::hostData<T>(); }

    void copyFromHost(const T *data, VkDeviceSize size, VkDeviceSize offset = 0) {
      return BufferMemory::copyFromHost((const void *)data, size * sizeof(T), offset * sizeof(T));
    }

    void copyFromHost(const std::vector<T> &data, VkDeviceSize offset = 0) {
      return BufferMemory::copyFromHost((const void *)data.data(), data.size() * sizeof(T), offset * sizeof(T));
    }

    // Length of the buffer in elements (T)
    size_t count(void) const { return this->getByteCount() / sizeof(T); }

    void resizeCount(size_t newCount) { this->resizeBytes(newCount * sizeof(T)); }
  };


  template <typename T, VkBufferUsageFlags usage,
            VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT>
  class FixedUsageTypedBuffer : public TypedBuffer<T> {
   public:
    FixedUsageTypedBuffer(size_t count)
        : TypedBuffer<T>(count, usage, properties) {}


    FixedUsageTypedBuffer(T *initial, size_t count)
        : TypedBuffer<T>(count, usage, properties) {
      this->copyFromHost(initial, count);
    }

    FixedUsageTypedBuffer(const std::vector<T> &initial)
        : TypedBuffer<T>(initial.size(), usage, properties) {
      this->copyFromHost(initial.data(), initial.size());
    }

    virtual ~FixedUsageTypedBuffer() = default;
  };




  template <typename T>
  using VertexBuffer = FixedUsageTypedBuffer<T, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT>;
  using IndexBuffer = FixedUsageTypedBuffer<u32, VK_BUFFER_USAGE_INDEX_BUFFER_BIT>;
  template <typename T>
  using UniformBuffer = FixedUsageTypedBuffer<T, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT>;
  template <typename T>
  using StorageBuffer = FixedUsageTypedBuffer<T, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT>;


  template <typename T>
  inline void bind(VkCommandBuffer cmd, VertexBuffer<T> &buf) {
    VkBuffer vertexBuffers[] = {buf.getHandle()};
    VkDeviceSize offsets[] = {0};


    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
  }


  inline void bind(VkCommandBuffer cmd, IndexBuffer &buf) { vkCmdBindIndexBuffer(cmd, buf.getHandle(), 0, VK_INDEX_TYPE_UINT32); }




  template <typename T>
  class UniformBufferSet {
    static constexpr size_t buffercount = 3;  // This is an engine constant.
   public:
    UniformBufferSet(size_t arrayLength = 1)
        : expectedArrayLength(arrayLength)
        , buffers(buffercount) {
      for (size_t i = 0; i < buffercount; ++i) {
        buffers[i] = std::make_shared<UniformBuffer<T>>(arrayLength);
      }
    }


    void setArrayLength(size_t newLength) { expectedArrayLength = newLength; }

    // Avoid using these methods! BufferMemory stays persistently mapped.
    T *map(void) { return getCurrentBuffer()->hostData(); }
    void unmap(void) {}

    // Copy data from host memory to the current buffer.
    void update(const T *data, VkDeviceSize count, VkDeviceSize offset = 0) { getCurrentBuffer()->copyFromHost(data, count, offset); }

    void update(const T &data) { update(&data, 1, 0); }
    VkBuffer getHandle() const { return getCurrentBuffer()->getHandle(); }
    const UniformBuffer<T> &currentAsBuffer() const { return *getCurrentBuffer(); }

    inline auto &getCurrentBuffer() const {
      auto index = ren::getFrameIndex();
      auto &buffer = buffers[index];
      if (buffer->getByteCount() != expectedArrayLength * sizeof(T)) {
        ren::dbgln("Frame {} of UBS isn't the right size. resizing from {} to {}", index, buffer->getByteCount(), expectedArrayLength * sizeof(T));
        buffer->resizeCount(expectedArrayLength);
      }
      return buffer;
    }

   private:
    size_t expectedArrayLength = 1;
    std::vector<ref<UniformBuffer<T>>> buffers;
  };




  template <typename T, VkBufferUsageFlags usage,
            VkMemoryPropertyFlags props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT>
  class ArenaBuffer : public FixedUsageTypedBuffer<T, usage, props> {
   public:
    ArenaBuffer(size_t initialCount)
        : FixedUsageTypedBuffer<T, usage, props>(initialCount) {}

    u64 allocate(size_t count = 1) {
      u64 current = bumpNext;
      bumpNext += count;
      if (bumpNext > this->count()) {
        // resize the buffer to accommodate the requested allocation
        size_t newSize = std::max(this->count() * 2, (size_t)bumpNext);
        this->resizeCount(newSize);
      }
      return current;
    }


    void ensure(u64 count) {
      if (count > this->count()) {
        this->resizeCount(count);
      }
    }

    void reset(u64 to = 0) {
      if (to <= this->count()) {
        bumpNext = to;
      }
    }

    u64 committed(void) const { return bumpNext; }

   private:
    u64 bumpNext = 0;
  };
}  // namespace ren
