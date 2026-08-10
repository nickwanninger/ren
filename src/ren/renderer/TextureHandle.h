#pragma once

#include <ren/types.h>

#include <compare>
#include <type_traits>

namespace ren {
  struct SampledImageIndex {
    u32 value = 0;
    auto operator<=>(const SampledImageIndex&) const = default;
  };

  struct SamplerIndex {
    u32 value = 0;
    auto operator<=>(const SamplerIndex&) const = default;
  };

  struct TextureHandle {
    u32 value = 0;
    auto operator<=>(const TextureHandle&) const = default;

    static constexpr u32 samplerBits = 8;
    static constexpr u32 samplerMask = (1u << samplerBits) - 1u;
    static constexpr u32 maxImageIndex = (1u << 24) - 1u;

    static constexpr TextureHandle pack(
        SampledImageIndex image, SamplerIndex sampler) {
      return {(image.value << samplerBits) | sampler.value};
    }
    constexpr SampledImageIndex image() const { return {value >> samplerBits}; }
    constexpr SamplerIndex sampler() const { return {value & samplerMask}; }
    constexpr bool valid() const { return image().value != 0; }
  };

  static_assert(sizeof(SampledImageIndex) == sizeof(u32));
  static_assert(sizeof(SamplerIndex) == sizeof(u32));
  static_assert(sizeof(TextureHandle) == sizeof(u32));
  static_assert(std::is_trivially_copyable_v<TextureHandle>);
}  // namespace ren
