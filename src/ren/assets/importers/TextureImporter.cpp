#include "TextureImporter.h"

namespace ren {

  void TextureImporter::import(ImportContext &context) {
    auto name = context.sourcePath.generic_string();
    auto bytes = context.readInput(context.sourcePath);

    auto texture = TextureAsset::decode(bytes.data(), bytes.size(), options.srgb ? PixelFormat::RGBA8_SRGB : PixelFormat::RGBA8_UNORM, name);
    texture->name = name;
    context.writeDerived(*texture);
  }

}  // namespace ren
