#pragma once

#include <ren/types.h>
#include <unordered_set>
#include <ren/renderer/vulkan/Vulkan.h>

namespace ren {




  // This represents the table and descriptor set creation for maintaining a single
  // descriptor heap used in descriptor indexing. The main use is for image views and
  // and samplers.



  // Traits must have
  // - static constexpr u32 MaxDescriptors
  // - static constexpr u32 BindingIndex
  // - using RenType = ren::ImageView or ren::Sampler
  // - using VkType = VkImageView or VkSampler
  // - static constexpr VkDescriptorType DescriptorType

  template <typename RenType, typename VkType, VkDescriptorType DescriptorType,
            int MaxDescriptors, int BindingIndex, int FirstDescriptor = 0>
  class DescriptorHeap {
   public:
    static constexpr u32 kMaxDescriptors = MaxDescriptors;
    static constexpr u32 kBindingIndex = BindingIndex;
    // We hold a reference to the underlying vulkan type so that we can manage
    // its lifetime and ensure it is not destroyed too early.
    using RenTypeRef = ref<RenType>;

   public:
    DescriptorHeap() {
      // 3 steps
      // 1. Create the descriptor pool
      createPool();
      // 2. Create the descriptor set layout for the descriptor set
      createLayout();
      // 3. Create the descriptor set
      createSet();

      // Then, we initialize the bump allocator.
      m_bump = FirstDescriptor;
    }


    ~DescriptorHeap() {
      // TODO: validate that nothing is in use somehow? Maybe just destruction order handles that fine.

      // teardown the descriptor pool
      if (m_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(getVulkan().device, m_pool, nullptr);
        m_pool = VK_NULL_HANDLE;
      }

      // delete the descriptor set layout
      if (m_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(getVulkan().device, m_layout, nullptr);
        m_layout = VK_NULL_HANDLE;
      }
    }


    void beginFrame() {
      // TODO: tick along and add things to the free list.
    }

    // allocate a slot for a descriptor.
    // TODO: take a RenTypeRef and track it.
    Option<u32> allocate(RenTypeRef resource) {
      u32 index = 0;

      // if the resource is already tracked, return the existing index.
      auto it = m_resources.find(resource);
      if (it != m_resources.end()) {
        return Some(it->second);
      }

      // If we have a free slot, use it.
      if (!m_freelist.empty()) {
        index = m_freelist.back();
        m_freelist.pop_back();
        // Track the resource in the map
        m_resources[resource] = index;
        writeDescriptor(index, vkHandle(resource));
        return Some(index);
      }

      // Otherwise, we bump the allocator.
      if (m_bump >= kMaxDescriptors) {
        return None; // Out of space!
      }

      index = m_bump++;
      m_resources[resource] = index;
      writeDescriptor(index, vkHandle(resource));

      return Some(index);
    }

    void release(u32 index, RenTypeRef resource) {
      // TODO: release it n frames later.
      // TODO: remove it from the resource map to release our reference.
    }


    VkDescriptorSet getSet() const { return m_set; }
    VkDescriptorSetLayout getLayout() const { return m_layout; }

   private:
    static VkType vkHandle(const RenTypeRef& resource) {
      if constexpr (std::is_same_v<VkType, VkImageView>) {
        return resource->getImageView();
      } else if constexpr (std::is_same_v<VkType, VkSampler>) {
        return resource->getHandle();
      } else {
        static_assert(std::is_same_v<VkType, void>,
                      "Unsupported VkType for DescriptorHeap");
      }
    }

    void createPool() {
      VkDescriptorPoolSize pool_size{};
      // We want a pool with kMaxDescriptors of the type specified in the Traits.
      pool_size.type = DescriptorType;
      pool_size.descriptorCount = kMaxDescriptors;

      VkDescriptorPoolCreateInfo pool_info = {};
      pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
      pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
      pool_info.maxSets = 1;        // we only need one set from this pool.
      pool_info.poolSizeCount = 1;  // Provide the pool_size
      pool_info.pPoolSizes = &pool_size;

      if (vkCreateDescriptorPool(getVulkan().device, &pool_info, nullptr, &m_pool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor pool");
      }
    }

    void createLayout() {
      // Manuacture a descriptor set layout with a single binding.
      VkDescriptorSetLayoutBinding binding{};
      binding.binding = kBindingIndex;
      binding.descriptorType = DescriptorType;
      binding.descriptorCount = kMaxDescriptors;
      binding.stageFlags = VK_SHADER_STAGE_ALL;  // every stage can access this descriptor set.
      binding.pImmutableSamplers = nullptr;

      // These flags change the semantics:
      // PARTIALLY_BOUND: Don't initialize everything up front.
      // UPDATE_AFTER_BIND: We can update the descriptor set freely after it has been bound.
      // UPDATE_UNUSED_WHILE_PENDING: Lets us update descriptors that aren't
      //    actually being used while command buffers referencing this set are pending.
      VkDescriptorBindingFlags binding_flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                                               VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;

      VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_info{};
      binding_flags_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
      binding_flags_info.bindingCount = 1;
      binding_flags_info.pBindingFlags = &binding_flags;

      VkDescriptorSetLayoutCreateInfo layout_info{};
      layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
      // Required when any binding uses UPDATE_AFTER_BIND.
      layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
      // Provide the binding
      layout_info.bindingCount = 1;
      layout_info.pBindings = &binding;
      // Descriptor binding flags are supplied through pNext.
      layout_info.pNext = &binding_flags_info;
      // Finally, create the layout!

      if (vkCreateDescriptorSetLayout(getVulkan().device, &layout_info, nullptr, &m_layout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor set layout");
      }
    }

    void createSet() {
      VkDescriptorSetAllocateInfo alloc_info{};
      alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;

      alloc_info.descriptorPool = m_pool;
      alloc_info.descriptorSetCount = 1;
      alloc_info.pSetLayouts = &m_layout;

      if (vkAllocateDescriptorSets(getVulkan().device, &alloc_info, &m_set) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate bindless descriptor set");
      }
    }


    void writeDescriptor(u32 index, VkType vkThing) {
      VkDescriptorImageInfo image_info{};
      if constexpr (std::is_same_v<VkType, VkImageView>) {
        image_info.imageView = vkThing;
        // The image must be properly configured for shader read access.
        // This is NOT the heap's job.
        image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      } else if constexpr (std::is_same_v<VkType, VkSampler>) {
        image_info.sampler = vkThing;
      } else {
        // throw a compile time error.
        static_assert(std::is_same_v<VkType, void>,
                      "Unsupported VkType for DescriptorHeap");
      }

      VkWriteDescriptorSet write{};
      write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      write.dstSet = m_set;
      write.dstBinding = kBindingIndex;
      write.dstArrayElement = index;
      write.descriptorCount = 1;
      write.descriptorType = DescriptorType;
      write.pImageInfo = &image_info;

      vkUpdateDescriptorSets(getVulkan().device, 1, &write, 0, nullptr);
    }

    u32 m_bump = 0;  // bump allocator.


    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
    VkDescriptorSet m_set = VK_NULL_HANDLE;


    // TODO: frame-safety!
    std::vector<u32> m_freelist;
    // We only map from the resource to the index.
    // it is up to the creator of the resource to retain the index somehow.
    std::unordered_map<RenTypeRef, u32> m_resources;
  };




  class Image; class Sampler;
  using ImageDescriptorHeap = DescriptorHeap<ren::Image, VkImageView,
      VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 64 * 1024, 0, 1>;
  using SamplerDescriptorHeap = DescriptorHeap<ren::Sampler, VkSampler,
      VK_DESCRIPTOR_TYPE_SAMPLER, 128, 0>;

}  // namespace ren
