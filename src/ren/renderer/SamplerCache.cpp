#include <ren/renderer/SamplerCache.h>

#include <type_traits>

namespace ren {

  std::size_t SamplerCache::DescHasher::operator()(const SamplerDesc& desc) const {
    // Simple order-sensitive combine; collisions just cost an operator==
    // comparison in the bucket, correctness only depends on operator==.
    std::size_t h = 0;
    auto combine = [&h](auto value) {
      using T = decltype(value);
      std::size_t v;
      if constexpr (std::is_floating_point_v<T>) {
        v = std::hash<T>{}(value);
      } else {
        v = std::hash<int>{}(static_cast<int>(value));
      }
      h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    };
    combine(desc.magFilter);
    combine(desc.minFilter);
    combine(desc.mipmapMode);
    combine(desc.addressModeU);
    combine(desc.addressModeV);
    combine(desc.addressModeW);
    combine(desc.maxAnisotropy);
    combine(desc.minLod);
    combine(desc.maxLod);
    return h;
  }

  ref<Sampler> SamplerCache::get(const SamplerDesc& desc) {
    std::lock_guard lock(mutex);
    auto it = entries.find(desc);
    if (it != entries.end()) {
      return it->second;
    }

    ref<Sampler> sampler(new Sampler(desc));
    sampler->descriptorIndex = {
        descriptorHeap->allocate(sampler).unwrap()};
    entries.emplace(desc, sampler);
    return sampler;
  }

}  // namespace ren
