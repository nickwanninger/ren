#pragma once

#include <mutex>
#include <utility>

#include <ren/renderer/Buffer.h>

namespace ren {
  struct ShaderABI {
    static constexpr u32 frameSet = 0;
    static constexpr u32 frameBinding = 0;
    static constexpr u32 sampledImageSet = 1;
    static constexpr u32 sampledImageBinding = 0;
    static constexpr u32 samplerSet = 2;
    static constexpr u32 samplerBinding = 0;
    static constexpr u32 pushConstantBytes = 128;
  };

  struct alignas(16) FrameGlobals {
    float time = 0.0f;
    float deltaTime = 0.0f;
    u32 frameNumber = 0;
    u32 _padding = 0;
    glm::vec2 renderSize{0.0f};
    glm::vec2 inverseRenderSize{0.0f};
    glm::mat4 viewMatrix{0.0f};
    glm::mat4 projMatrix{0.0f};
  };
  static_assert(sizeof(FrameGlobals) == 160);
  static_assert(offsetof(FrameGlobals, viewMatrix) == 32);
  static_assert(offsetof(FrameGlobals, projMatrix) == 96);

  class FrameGlobalsAllocator;

  // Owns one descriptor set from a FrameGlobalsAllocator and gives it back on
  // destruction. Like the command buffer of a SubmissionUnit, the owner must
  // make sure the GPU no longer uses the set before it is destroyed.
  class FrameGlobalsBinding {
   public:
    FrameGlobalsBinding(const FrameGlobalsBinding&) = delete;
    FrameGlobalsBinding& operator=(const FrameGlobalsBinding&) = delete;
    FrameGlobalsBinding(FrameGlobalsBinding&& other) noexcept
        : allocator(std::exchange(other.allocator, nullptr)),
          buffer(std::move(other.buffer)),
          descriptorSet(std::exchange(other.descriptorSet, VK_NULL_HANDLE)) {}
    FrameGlobalsBinding& operator=(FrameGlobalsBinding&& other) noexcept {
      if (this != &other) {
        release();
        allocator = std::exchange(other.allocator, nullptr);
        buffer = std::move(other.buffer);
        descriptorSet = std::exchange(other.descriptorSet, VK_NULL_HANDLE);
      }
      return *this;
    }
    ~FrameGlobalsBinding() { release(); }

    void set(const FrameGlobals& globals) {
      buffer.copyFromHost(&globals, sizeof(globals));
    }

    VkDescriptorSet getSet() const { return descriptorSet; }

   private:
    friend class FrameGlobalsAllocator;
    FrameGlobalsBinding(FrameGlobalsAllocator* allocator, BufferMemory buffer,
                        VkDescriptorSet descriptorSet)
        : allocator(allocator), buffer(std::move(buffer)), descriptorSet(descriptorSet) {}

    inline void release();

    FrameGlobalsAllocator* allocator = nullptr;
    BufferMemory buffer;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
  };

  class FrameGlobalsAllocator {
   public:
    static constexpr u32 capacity = 128;

    FrameGlobalsAllocator() {
      VkDescriptorSetLayoutBinding binding{
          .binding = ShaderABI::frameBinding,
          .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_ALL};
      VkDescriptorSetLayoutCreateInfo layoutInfo{
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
          .bindingCount = 1,
          .pBindings = &binding};
      if (vkCreateDescriptorSetLayout(
              getVulkan().device, &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create FrameGlobals descriptor layout");
      }

      VkDescriptorPoolSize poolSize{
          .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .descriptorCount = capacity};
      VkDescriptorPoolCreateInfo poolInfo{
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
          .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
          .maxSets = capacity,
          .poolSizeCount = 1,
          .pPoolSizes = &poolSize};
      if (vkCreateDescriptorPool(
              getVulkan().device, &poolInfo, nullptr, &pool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create FrameGlobals descriptor pool");
      }
    }

    ~FrameGlobalsAllocator() {
      if (pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(getVulkan().device, pool, nullptr);
      }
      if (layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(getVulkan().device, layout, nullptr);
      }
    }

    FrameGlobalsBinding allocate() {
      std::lock_guard lock(mutex);

      VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
      VkDescriptorSetAllocateInfo allocateInfo{
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
          .descriptorPool = pool,
          .descriptorSetCount = 1,
          .pSetLayouts = &layout};
      if (vkAllocateDescriptorSets(
              getVulkan().device, &allocateInfo, &descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate FrameGlobals descriptor set");
      }

      auto buffer = TypedBuffer<FrameGlobals>(
          1, BufferDomain::Upload, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
      VkDescriptorBufferInfo bufferInfo{
          .buffer = buffer.getHandle(),
          .offset = 0,
          .range = sizeof(FrameGlobals)};
      VkWriteDescriptorSet write{
          .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .dstSet = descriptorSet,
          .dstBinding = ShaderABI::frameBinding,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .pBufferInfo = &bufferInfo};
      vkUpdateDescriptorSets(getVulkan().device, 1, &write, 0, nullptr);

      FrameGlobalsBinding result(this, std::move(buffer), descriptorSet);
      result.set({});
      return result;
    }

    VkDescriptorSetLayout getLayout() const { return layout; }

   private:
    friend class FrameGlobalsBinding;

    void free(VkDescriptorSet descriptorSet) {
      std::lock_guard lock(mutex);
      vkFreeDescriptorSets(getVulkan().device, pool, 1, &descriptorSet);
    }

    std::mutex mutex;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
  };

  inline void FrameGlobalsBinding::release() {
    if (allocator != nullptr && descriptorSet != VK_NULL_HANDLE) {
      allocator->free(descriptorSet);
    }
    allocator = nullptr;
    descriptorSet = VK_NULL_HANDLE;
  }
}  // namespace ren
