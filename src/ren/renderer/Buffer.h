#pragma once

#include <ren/types.h>
#include <vulkan/vulkan_core.h>
#include <vector>

namespace ren {
  class VulkanInstance;

  // Represents a buffer in Vulkan memory.
  class Buffer {
   public:
    Buffer(VulkanInstance &vulkan_instance, VkDeviceSize size, VkBufferUsageFlags usage,
           VkMemoryPropertyFlags properties);

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
    VkDeviceSize getSize() const { return size; }
    bool isMapped() const { return mapped != nullptr; }
    const std::string &getName() const { return name; }
    void setName(const std::string &new_name);

    void resize(size_t new_bytes);


   protected:
    VulkanInstance &vulkan;
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
    TypedBuffer(VulkanInstance &vulkan, size_t count, VkBufferUsageFlags usage,
                VkMemoryPropertyFlags properties)
        : Buffer(vulkan, count * sizeof(T), usage, properties) {}
    virtual ~TypedBuffer() = default;


    // Wrap the map function in something typed
    T *map(void) { return Buffer::map(); }

    void copyFromHost(const T *data, VkDeviceSize size, VkDeviceSize offset = 0) {
      return Buffer::copyFromHost((const void *)data, size * sizeof(T), offset);
    }

    void copyFromHost(const std::vector<T> &data, VkDeviceSize offset = 0) {
      return Buffer::copyFromHost((const void *)data.data(), data.size() * sizeof(T), offset);
    }
  };


  template <typename T, VkBufferUsageFlags usage,
            VkMemoryPropertyFlags properties =
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT>
  class FixedUsageTypedBuffer : public TypedBuffer<T> {
   public:
    FixedUsageTypedBuffer(VulkanInstance &vulkan, size_t count)
        : TypedBuffer<T>(vulkan, count, usage, properties) {}


    FixedUsageTypedBuffer(VulkanInstance &vulkan, T *initial, size_t count)
        : TypedBuffer<T>(vulkan, count, usage, properties) {
      this->copyFromHost(initial, count);
    }

    FixedUsageTypedBuffer(VulkanInstance &vulkan, const std::vector<T> &initial)
        : TypedBuffer<T>(vulkan, initial.size(), usage, properties) {
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

}  // namespace ren
