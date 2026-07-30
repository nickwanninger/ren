#include <ren/renderer/GlobalDescriptors.h>

#include <array>

#include <ren/renderer/Buffer.h>
#include <ren/renderer/Image.h>
#include <ren/renderer/Sampler.h>

namespace ren {
  namespace {
    void check(VkResult result, std::string_view operation) {
      if (result != VK_SUCCESS) {
        throw std::runtime_error(
            fmt::format("{} failed with VkResult {}", operation, static_cast<int>(result)));
      }
    }
  }  // namespace

  GlobalDescriptors::GlobalDescriptors() {
    auto device = getVulkan().device;

    VkDescriptorSetLayoutBinding frameBinding{
        .binding = GlobalDescriptorABI::frameBinding,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_ALL,
    };
    VkDescriptorSetLayoutCreateInfo frameLayoutInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &frameBinding,
    };
    check(
        vkCreateDescriptorSetLayout(device, &frameLayoutInfo, nullptr, &m_frameLayout),
        "creating frame descriptor layout");

    std::array heapBindings{
        VkDescriptorSetLayoutBinding{
            .binding = GlobalDescriptorABI::samplerBinding,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
            .descriptorCount = GlobalDescriptorABI::maxSamplers,
            .stageFlags = VK_SHADER_STAGE_ALL,
        },
        VkDescriptorSetLayoutBinding{
            .binding = GlobalDescriptorABI::sampledImageBinding,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .descriptorCount = GlobalDescriptorABI::maxSampledImages,
            .stageFlags = VK_SHADER_STAGE_ALL,
        },
        VkDescriptorSetLayoutBinding{
            .binding = GlobalDescriptorABI::storageImageBinding,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = GlobalDescriptorABI::maxStorageImages,
            .stageFlags = VK_SHADER_STAGE_ALL,
        },
    };
    constexpr VkDescriptorBindingFlags bindingFlags =
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
    std::array<VkDescriptorBindingFlags, heapBindings.size()> heapBindingFlags{
        bindingFlags, bindingFlags, bindingFlags};
    VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = static_cast<u32>(heapBindingFlags.size()),
        .pBindingFlags = heapBindingFlags.data(),
    };
    VkDescriptorSetLayoutCreateInfo heapLayoutInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &flagsInfo,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = static_cast<u32>(heapBindings.size()),
        .pBindings = heapBindings.data(),
    };
    check(
        vkCreateDescriptorSetLayout(device, &heapLayoutInfo, nullptr, &m_heapLayout),
        "creating bindless descriptor layout");

    std::array poolSizes{
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 32},
        VkDescriptorPoolSize{
            VK_DESCRIPTOR_TYPE_SAMPLER, GlobalDescriptorABI::maxSamplers},
        VkDescriptorPoolSize{
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            GlobalDescriptorABI::maxSampledImages},
        VkDescriptorPoolSize{
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            GlobalDescriptorABI::maxStorageImages},
    };
    VkDescriptorPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT |
                 VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = 33,
        .poolSizeCount = static_cast<u32>(poolSizes.size()),
        .pPoolSizes = poolSizes.data(),
    };
    check(
        vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_pool),
        "creating global descriptor pool");

    VkDescriptorSetAllocateInfo allocationInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = m_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &m_heapLayout,
    };
    check(
        vkAllocateDescriptorSets(device, &allocationInfo, &m_heapSet),
        "allocating bindless descriptor set");
  }

  GlobalDescriptors::~GlobalDescriptors() {
    auto device = getVulkan().device;
    m_sampledImages.clear();
    m_storageImages.clear();
    if (m_pool != VK_NULL_HANDLE) {
      vkDestroyDescriptorPool(device, m_pool, nullptr);
    }
    if (m_heapLayout != VK_NULL_HANDLE) {
      vkDestroyDescriptorSetLayout(device, m_heapLayout, nullptr);
    }
    if (m_frameLayout != VK_NULL_HANDLE) {
      vkDestroyDescriptorSetLayout(device, m_frameLayout, nullptr);
    }
  }

  VkDescriptorSet GlobalDescriptors::allocateFrameSet(const BufferMemory& buffer) {
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo allocationInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = m_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &m_frameLayout,
    };
    check(
        vkAllocateDescriptorSets(getVulkan().device, &allocationInfo, &set),
        "allocating frame descriptor set");

    VkDescriptorBufferInfo bufferInfo{
        .buffer = buffer.getHandle(),
        .offset = 0,
        .range = buffer.getByteCount(),
    };
    VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set,
        .dstBinding = GlobalDescriptorABI::frameBinding,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo = &bufferInfo,
    };
    vkUpdateDescriptorSets(getVulkan().device, 1, &write, 0, nullptr);
    return set;
  }

  void GlobalDescriptors::freeFrameSet(VkDescriptorSet set) {
    if (set != VK_NULL_HANDLE) {
      vkFreeDescriptorSets(getVulkan().device, m_pool, 1, &set);
    }
  }

  SampledImageHandle GlobalDescriptors::registerSampledImage(
      ref<Image> image, VkImageLayout layout) {
    if (!image) {
      throw std::runtime_error("Cannot register a null sampled image");
    }
    if (m_nextSampledImage >= GlobalDescriptorABI::maxSampledImages) {
      throw std::runtime_error("Sampled image descriptor heap is full");
    }
    SampledImageHandle handle{m_nextSampledImage++};
    replace(handle, std::move(image), layout);
    return handle;
  }

  StorageImageHandle GlobalDescriptors::registerStorageImage(
      ref<Image> image, VkImageLayout layout) {
    if (!image) {
      throw std::runtime_error("Cannot register a null storage image");
    }
    if (m_nextStorageImage >= GlobalDescriptorABI::maxStorageImages) {
      throw std::runtime_error("Storage image descriptor heap is full");
    }
    StorageImageHandle handle{m_nextStorageImage++};
    replace(handle, std::move(image), layout);
    return handle;
  }

  SamplerHandle GlobalDescriptors::registerSampler(const Sampler& sampler) {
    if (m_nextSampler >= GlobalDescriptorABI::maxSamplers) {
      throw std::runtime_error("Sampler descriptor heap is full");
    }
    SamplerHandle handle{m_nextSampler++};
    replace(handle, sampler);
    return handle;
  }

  void GlobalDescriptors::replace(
      SampledImageHandle handle, ref<Image> image, VkImageLayout layout) {
    if (!image || handle.index >= m_nextSampledImage) {
      throw std::runtime_error("Invalid sampled image descriptor replacement");
    }
    if (m_sampledImages.size() <= handle.index) {
      m_sampledImages.resize(handle.index + 1);
    }
    m_sampledImages[handle.index] = image;
    VkDescriptorImageInfo imageInfo{
        .imageView = image->getImageView(),
        .imageLayout = layout,
    };
    VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = m_heapSet,
        .dstBinding = GlobalDescriptorABI::sampledImageBinding,
        .dstArrayElement = handle.index,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .pImageInfo = &imageInfo,
    };
    vkUpdateDescriptorSets(getVulkan().device, 1, &write, 0, nullptr);
  }

  void GlobalDescriptors::replace(
      StorageImageHandle handle, ref<Image> image, VkImageLayout layout) {
    if (!image || handle.index >= m_nextStorageImage) {
      throw std::runtime_error("Invalid storage image descriptor replacement");
    }
    if (m_storageImages.size() <= handle.index) {
      m_storageImages.resize(handle.index + 1);
    }
    m_storageImages[handle.index] = image;
    VkDescriptorImageInfo imageInfo{
        .imageView = image->getImageView(),
        .imageLayout = layout,
    };
    VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = m_heapSet,
        .dstBinding = GlobalDescriptorABI::storageImageBinding,
        .dstArrayElement = handle.index,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo = &imageInfo,
    };
    vkUpdateDescriptorSets(getVulkan().device, 1, &write, 0, nullptr);
  }

  void GlobalDescriptors::replace(SamplerHandle handle, const Sampler& sampler) {
    if (handle.index >= m_nextSampler) {
      throw std::runtime_error("Invalid sampler descriptor replacement");
    }
    VkDescriptorImageInfo imageInfo{.sampler = sampler.getHandle()};
    VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = m_heapSet,
        .dstBinding = GlobalDescriptorABI::samplerBinding,
        .dstArrayElement = handle.index,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
        .pImageInfo = &imageInfo,
    };
    vkUpdateDescriptorSets(getVulkan().device, 1, &write, 0, nullptr);
  }

  void GlobalDescriptors::bind(
      VkCommandBuffer cmd,
      VkPipelineBindPoint bindPoint,
      VkPipelineLayout pipelineLayout,
      VkDescriptorSet frameSet) const {
    std::array sets{frameSet, m_heapSet};
    vkCmdBindDescriptorSets(
        cmd, bindPoint, pipelineLayout, 0, static_cast<u32>(sets.size()),
        sets.data(), 0, nullptr);
  }
}  // namespace ren
