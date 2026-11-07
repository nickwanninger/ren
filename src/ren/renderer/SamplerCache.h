#pragma once

#include <ren/types.h>
#include <ren/renderer/DescriptorHeap.h>
#include <ren/renderer/Sampler.h>

#include <mutex>
#include <unordered_map>

namespace ren {
  // Sole Sampler factory. Entries are retained for the renderer lifetime.
  class SamplerCache : public VulkanResource {
   public:
    explicit SamplerCache(ref<SamplerDescriptorHeap> descriptorHeap)
        : descriptorHeap(std::move(descriptorHeap)) {}
    ref<Sampler> get(const SamplerDesc& desc);

   private:
    struct DescHasher { std::size_t operator()(const SamplerDesc&) const; };
    std::mutex mutex;
    ref<SamplerDescriptorHeap> descriptorHeap;
    std::unordered_map<SamplerDesc, ref<Sampler>, DescHasher> entries;
  };
}  // namespace ren
