#pragma once

#include <ren/assets/TextureAsset.h>
#include <ren/assets/importers/Importer.h>

namespace ren {

  // Converts a standalone image into one RGBA8 texture bundle.
  class TextureImporter : public Importer {
   public:
    struct Options {
      // Decode as sRGB color. Set this to false for normal maps and other data textures.
      bool srgb = true;

      NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Options, srgb)
    };

    static std::vector<std::string> getExtensions(void) { return {".png", ".jpg", ".jpeg", ".tga"}; }

    explicit TextureImporter(Options options)
        : options(std::move(options)) {}

    void import(ImportContext &context) override;

   private:
    Options options;
  };

}  // namespace ren
