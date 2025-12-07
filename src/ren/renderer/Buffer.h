#pragma once

#include <ren/types.h>
#include <ren/core/Instrumentation.h>
#include <vulkan/vulkan_core.h>
#include <ren/renderer/Swapchain.h>  // To get the current frame index.
#include <vector>

namespace ren {
  class VulkanInstance;

  // Represents a buffer in Vulkan memory.
  class Buffer {
   public:
    Buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);

    virtual ~Buffer();

    // Non-copyable, movable
    Buffer(const Buffer &) = delete;
    Buffer &operator=(const Buffer &) = delete;
    Buffer(Buffer &&other) noexcept;
    Buffer &operator=(Buffer &&other) noexcept;


    void *map(void);
    void unmap(void);

    void copyFrom(const Buffer &src, VkDeviceSize size, VkDeviceSize srcOffset = 0,
                  VkDeviceSize dstOffset = 0);
    void copyFromHost(const void *data, VkDeviceSize size, VkDeviceSize offset = 0);


    // Getters
    VkBuffer getHandle() const { return buffer; }
    // Size of the buffer in bytes.
    VkDeviceSize getSize() const { return size; }
    bool isMapped() const { return mapped != nullptr; }
    const std::string &getName() const { return name; }
    void setName(const std::string &new_name);

    // Resize the buffer to a certain byte count.
    void resizeBytes(size_t new_bytes);


   protected:
    std::string name = "UnnamedBuffer";

    VmaAllocation allocation = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize size = 0;

    VkBufferUsageFlags usage;
    VkMemoryPropertyFlags properties;

    void *mapped = nullptr;
  };


  template <typename T>
  class TypedBuffer : public Buffer {
   public:
    using EntryType = T;
    TypedBuffer(size_t count, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties)
        : Buffer(count * sizeof(T), usage, properties) {}
    virtual ~TypedBuffer() = default;


    // Wrap the map function in something typed
    T *map(void) { return (T *)Buffer::map(); }

    void copyFromHost(const T *data, VkDeviceSize size, VkDeviceSize offset = 0) {
      return Buffer::copyFromHost((const void *)data, size * sizeof(T), offset * sizeof(T));
    }

    void copyFromHost(const std::vector<T> &data, VkDeviceSize offset = 0) {
      return Buffer::copyFromHost((const void *)data.data(), data.size() * sizeof(T), offset);
    }

    // Length of the buffer in elements (T)
    size_t count(void) const { return this->getSize() / sizeof(T); }

    // Resize the buffer to a certain element count.
    void resizeCount(size_t new_count) { this->resizeBytes(new_count * sizeof(T)); }
  };


  template <typename T, VkBufferUsageFlags usage,
            VkMemoryPropertyFlags properties =
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT>
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
  inline void bind(VkCommandBuffer cmd, VertexBuffer<T> &buf) {
    VkBuffer vertexBuffers[] = {buf.getHandle()};
    VkDeviceSize offsets[] = {0};


    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
  }


  inline void bind(VkCommandBuffer cmd, IndexBuffer &buf) {
    vkCmdBindIndexBuffer(cmd, buf.getHandle(), 0, VK_INDEX_TYPE_UINT32);
  }




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


    // Avoid using these methods!
    T *map(void) { return getCurrentBuffer()->map(); }
    void unmap(void) { getCurrentBuffer()->unmap(); }

    // Copy data from host memory to the current buffer.
    void update(const T *data, VkDeviceSize count, VkDeviceSize offset = 0) {
      getCurrentBuffer()->copyFromHost(data, count, offset);
    }

    void update(const T &data) { update(&data, 1, 0); }
    VkBuffer getHandle() const { return getCurrentBuffer()->getHandle(); }
    const Buffer &currentAsBuffer() const { return *getCurrentBuffer(); }

    inline auto &getCurrentBuffer() const {
      auto index = ren::getFrameIndex();
      auto &buffer = buffers[index];
      if (buffer->getSize() != expectedArrayLength * sizeof(T)) {
        // resize the buffer!
        fmt::println("Frame {} of UBS isn't the right size. resizing from {} to {}", index,
                     buffer->getSize(), expectedArrayLength * sizeof(T));
        buffer->resizeCount(expectedArrayLength);
      }
      return buffer;
    }

   private:
    size_t expectedArrayLength = 1;
    std::vector<ref<UniformBuffer<T>>> buffers;
  };




  template <typename T, VkBufferUsageFlags usage,
            VkMemoryPropertyFlags props =
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT>
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
      if (count > this->count()) { this->resizeCount(count); }
    }

    void reset(u64 to = 0) {
      if (to <= this->count()) bumpNext = to;
    }

    u64 committed(void) const { return bumpNext; }

   private:
    u64 bumpNext = 0;
  };
}  // namespace ren
