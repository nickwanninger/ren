#pragma once

#include <ren/types.h>
#include <ren/renderer/vulkan/Vulkan.h>

namespace ren {
  class BufferMemory;
  class Image;
  class Sampler;

  // The native Vulkan shader ABI. Set 0 is one per-submission uniform buffer.
  // Set 1 is Slang's typed bindless heap using BindlessDescriptorOptions.None:
  // samplers, combined image samplers, sampled images, then storage images.
  struct GlobalDescriptorABI {
    static constexpr u32 frameSet = 0;
    static constexpr u32 frameBinding = 0;
    static constexpr u32 heapSet = 1;
    static constexpr u32 samplerBinding = 0;
    static constexpr u32 combinedImageSamplerBinding = 1;
    static constexpr u32 sampledImageBinding = 2;
    static constexpr u32 storageImageBinding = 3;

    // Sampler and combined-image-sampler descriptors share Vulkan's sampler
    // limit. MoltenVK exposes 80, so keep their total at 80.
    static constexpr u32 maxSamplers = 16;
    static constexpr u32 maxCombinedImageSamplers = 64;
    static constexpr u32 maxSampledImages = 4096;
    static constexpr u32 maxStorageImages = 1024;
    static constexpr u32 pushConstantBytes = 128;
  };

  struct alignas(16) FrameGlobals {
    float time = 0.0f;
    float deltaTime = 0.0f;
    u32 frameNumber = 0;
    u32 _padding = 0;
    glm::vec2 renderSize{0.0f};
    glm::vec2 inverseRenderSize{0.0f};
  };
  static_assert(sizeof(FrameGlobals) == 32);

  // DescriptorHandle<T> is uint2 on Slang's SPIR-V target. The first word is
  // the array index. The second is reserved for descriptor kinds that need it.
  template <typename Tag>
  struct DescriptorHandle {
    u32 index = 0;
    u32 auxiliary = 0;
    auto operator<=>(const DescriptorHandle&) const = default;
  };

  struct SampledImageTag;
  struct StorageImageTag;
  struct SamplerTag;
  struct CombinedImageSamplerTag;
  using SampledImageHandle = DescriptorHandle<SampledImageTag>;
  using StorageImageHandle = DescriptorHandle<StorageImageTag>;
  using SamplerHandle = DescriptorHandle<SamplerTag>;
  using CombinedImageSamplerHandle =
      DescriptorHandle<CombinedImageSamplerTag>;
  static_assert(sizeof(SampledImageHandle) == sizeof(u32) * 2);
  static_assert(sizeof(StorageImageHandle) == sizeof(u32) * 2);
  static_assert(sizeof(SamplerHandle) == sizeof(u32) * 2);
  static_assert(sizeof(CombinedImageSamplerHandle) == sizeof(u32) * 2);

  class GlobalDescriptors : public VulkanResource {
   public:
    GlobalDescriptors();
    ~GlobalDescriptors();

    GlobalDescriptors(const GlobalDescriptors&) = delete;
    GlobalDescriptors& operator=(const GlobalDescriptors&) = delete;

    VkDescriptorSetLayout frameLayout() const { return m_frameLayout; }
    VkDescriptorSetLayout heapLayout() const { return m_heapLayout; }
    VkDescriptorSet allocateFrameSet(const BufferMemory& buffer);
    void freeFrameSet(VkDescriptorSet set);

    SampledImageHandle registerSampledImage(
        ref<Image> image,
        VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    StorageImageHandle registerStorageImage(
        ref<Image> image,
        VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL);
    SamplerHandle registerSampler(const Sampler& sampler);
    CombinedImageSamplerHandle registerCombinedImageSampler(
        ref<Image> image,
        VkSampler sampler,
        VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    void replace(
        SampledImageHandle handle,
        ref<Image> image,
        VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    void replace(
        StorageImageHandle handle,
        ref<Image> image,
        VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL);
    void replace(SamplerHandle handle, const Sampler& sampler);
    void replace(
        CombinedImageSamplerHandle handle,
        ref<Image> image,
        VkSampler sampler,
        VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    // The caller must ensure no in-flight GPU work still uses this handle,
    // matching the lifetime requirement for destroying its image and sampler.
    void release(CombinedImageSamplerHandle handle);

    void bind(
        VkCommandBuffer cmd,
        VkPipelineBindPoint bindPoint,
        VkPipelineLayout pipelineLayout,
        VkDescriptorSet frameSet) const;

   private:
    VkDescriptorSetLayout m_frameLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_heapLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSet m_heapSet = VK_NULL_HANDLE;

    u32 m_nextSampledImage = 0;
    u32 m_nextStorageImage = 0;
    u32 m_nextSampler = 0;
    std::vector<ref<Image>> m_sampledImages;
    std::vector<ref<Image>> m_storageImages;

    struct CombinedImageSamplerSlot {
      ref<Image> image;
      VkSampler sampler = VK_NULL_HANDLE;
      u32 generation = 1;
    };
    std::vector<CombinedImageSamplerSlot> m_combinedImageSamplers;
    std::vector<u32> m_freeCombinedImageSamplers;
  };
}  // namespace ren
