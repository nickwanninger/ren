#include <gtest/gtest.h>
#include <ren/renderer/TextureHandle.h>

namespace ren::test {
  TEST(TextureHandle, ZeroImageIsInvalid) {
    const auto handle = TextureHandle::pack({0}, {255});
    EXPECT_FALSE(handle.valid());
    EXPECT_EQ(handle.image().value, 0u);
    EXPECT_EQ(handle.sampler().value, 255u);
  }

  TEST(TextureHandle, PacksBoundaryValues) {
    const auto handle = TextureHandle::pack(
        {TextureHandle::maxImageIndex}, {TextureHandle::samplerMask});
    EXPECT_TRUE(handle.valid());
    EXPECT_EQ(handle.value, 0xffffffffu);
    EXPECT_EQ(handle.image().value, TextureHandle::maxImageIndex);
    EXPECT_EQ(handle.sampler().value, TextureHandle::samplerMask);
  }
}  // namespace ren::test
