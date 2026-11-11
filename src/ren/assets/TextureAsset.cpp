#include <ren/assets/TextureAsset.h>
#include <ren/core/Bundle.h>
#include <ren/renderer/Texture.h>

#include <limits>

namespace ren {

  void TextureAsset::validate(void) const {
    u64 pixelCount = u64(width) * height;
    if (width == 0 || height == 0 || pixelCount > std::numeric_limits<size_t>::max() / 4 || pixels.size() != pixelCount * 4) {
      throw std::runtime_error("Invalid RGBA8 texture");
    }
  }


  void TextureAsset::write(Bundle &bundle) const {
    bundle.setKey("width", width);
    bundle.setKey("height", height);
    bundle.setKey("pixelFormat", pixelFormat);
    bundle.attachBlob("pixels", pixels);
  }


  ref<TextureAsset> TextureAsset::read(const Bundle &bundle) {
    const auto &meta = bundle.getMetadata();

    auto texture = make<TextureAsset>();
    texture->width = meta.at("width").get<u32>();
    texture->height = meta.at("height").get<u32>();
    texture->pixelFormat = parseEnum<PixelFormat>(meta.at("pixelFormat"), "pixel format");
    texture->pixels = bundle.readBlob<u8>("pixels");
    return texture;
  }


  ref<TextureAsset> TextureAsset::decode(const void *data, size_t size, PixelFormat format, const std::string &name) {
    // The decoder takes an int length.
    if (size == 0 || size > std::numeric_limits<int>::max()) {
      throw std::runtime_error("Encoded texture is empty or exceeds decoder limit: " + name);
    }

    auto image = Texture::decode(data, size);
    if (!image) {
      throw std::runtime_error("Cannot decode texture: " + name);
    }

    auto texture = make<TextureAsset>();
    texture->width = image.width;
    texture->height = image.height;
    texture->pixelFormat = format;
    texture->pixels.assign(image.pixels.get(), image.pixels.get() + size_t(image.width) * image.height * 4);
    return texture;
  }

}  // namespace ren
