#pragma once

#include <ren/renderer/Vulkan.h>


// This set of classes implements a thin wrapper around Vulkan descriptor sets.
// Much of the design is mostly stolen from
// https://vkguide.dev/docs/extra-chapter/abstracting_descriptors/
namespace ren {

  static constexpr u32 descriptorSetPoolSize = 1000;

  class DescriptorAllocator {
   public:
    struct PoolSizes {
      std::vector<std::pair<VkDescriptorType, float>> sizes = {
          {VK_DESCRIPTOR_TYPE_SAMPLER, 0.5f},
          {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4.f},
          {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4.f},
          {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1.f},
          {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1.f},
          {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1.f},
          {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2.f},
          {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2.f},
          {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1.f},
          {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1.f},
          {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 0.5f}};
    };

    DescriptorAllocator();
    ~DescriptorAllocator();

    void reset_pools();
    bool allocate(VkDescriptorSet* set, VkDescriptorSetLayout layout);


   private:
    VkDevice device;  // from getVulkan().device
    VkDescriptorPool grab_pool();

    VkDescriptorPool currentPool{VK_NULL_HANDLE};
    PoolSizes descriptorSizes;
    std::vector<VkDescriptorPool> usedPools;
    std::vector<VkDescriptorPool> freePools;
  };


  // ------------------------------------------------------ //

  struct DescriptorLayoutInfo {
    // good idea to turn this into a inlined array
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bool operator==(const DescriptorLayoutInfo& other) const;
    size_t hash() const;

    // Add bindings to a specific slot
    void addBinding(uint32_t binding, VkDescriptorType type, uint32_t count,
                    VkShaderStageFlags stageFlags, const VkSampler* immutableSampler = nullptr);
  };


  // ------------------------------------------------------ //

  // Now, we will implement a descriptor set layout cache. This class is useful
  // because it will allow us to cache descriptor set layouts and reuse them
  // based on the types of descriptors we need in a given layout.
  class DescriptorLayoutCache {
   public:
    ~DescriptorLayoutCache();

    VkDescriptorSetLayout createLayout(VkDescriptorSetLayoutCreateInfo* info);
    VkDescriptorSetLayout createLayout(DescriptorLayoutInfo& info);

    float hitrate(void) const { return (float)cacheHits / (cacheHits + cacheMisses); }

   private:
    u64 cacheHits = 0;
    u64 cacheMisses = 0;
    struct DescriptorLayoutHash {
      std::size_t operator()(const DescriptorLayoutInfo& k) const { return k.hash(); }
    };

    std::unordered_map<DescriptorLayoutInfo, VkDescriptorSetLayout, DescriptorLayoutHash>
        layoutCache;
  };


  // ------------------------------------------------------ //

  class DescriptorBuilder {
   public:
    DescriptorBuilder(DescriptorLayoutCache& layoutCache, DescriptorAllocator& allocator);

    DescriptorBuilder& bindBuffer(uint32_t binding, VkDescriptorBufferInfo* bufferInfo,
                                  VkDescriptorType type, VkShaderStageFlags stageFlags);
    DescriptorBuilder& bindImage(uint32_t binding, VkDescriptorImageInfo* imageInfo,
                                 VkDescriptorType type, VkShaderStageFlags stageFlags);

    bool build(VkDescriptorSet& set, VkDescriptorSetLayout& layout);
    bool build(VkDescriptorSet& set);

   private:
    std::vector<VkWriteDescriptorSet> writes;
    std::vector<VkDescriptorSetLayoutBinding> bindings;

    DescriptorLayoutCache& cache;
    DescriptorAllocator& alloc;
  };

}  // namespace ren