#pragma once

#include <ren/assets/Asset.h>

namespace ren {

  // Pixel layout of a texture asset. Both formats store 4 bytes per pixel.
  enum class PixelFormat : u8 {
    RGBA8_SRGB,
    RGBA8_UNORM,
  };

  JSON_SERIALIZE_ENUM(PixelFormat, {
                                       {PixelFormat::RGBA8_SRGB, "RGBA8_SRGB"},
                                       {PixelFormat::RGBA8_UNORM, "RGBA8_UNORM"},
                                   })


  // Decoded RGBA8 pixels of one image, before GPU upload.
  class TextureAsset : public Asset {
   public:
    static constexpr AssetType TYPE = AssetType::Texture;

    u32 width = 0;
    u32 height = 0;
    PixelFormat pixelFormat = PixelFormat::RGBA8_SRGB;
    std::vector<u8> pixels;

    AssetType getType(void) const override { return TYPE; }

    // Throws if the size is zero or the pixel count does not match the size.
    void validate(void) const override;

    void write(Bundle &bundle) const override;

    static ref<TextureAsset> read(const Bundle &bundle);

    // Decode an encoded image (PNG, JPEG, ...) into a texture asset. Does no GPU work.
    // Throws an error that names `name` if the image is empty, too large, or invalid.
    static ref<TextureAsset> decode(const void *data, size_t size, PixelFormat format, const std::string &name);
  };

}  // namespace ren
