// Tests of AssetManager::resolve(): an AssetID becomes a runtime object, the same object on each
// call, and a bad ID gives None. The tests make no viewport, so they have no golden image.

#include "RenderTest.h"

#include <ren/assets/AssetManager.h>
#include <ren/assets/Handle.h>
#include <ren/assets/Material.h>
#include <ren/assets/TextureAsset.h>
#include <ren/renderer/Texture.h>

#include <filesystem>

namespace {

  struct TemporaryAssets {
    std::filesystem::path root = std::filesystem::temp_directory_path() / ("ren-handle-test-" + ren::AssetID::generate().to_string());

    TemporaryAssets() { std::filesystem::create_directories(root); }

    ~TemporaryAssets() {
      std::error_code error;
      std::filesystem::remove_all(root, error);
    }
  };


  // Import one file of the test assets into `manager`, and add its outputs to the library.
  json importTestFile(ren::AssetManager &manager, const std::filesystem::path &root, const std::filesystem::path &file) {
    std::filesystem::copy_file(file, root / file.filename());
    auto record = manager.importFile(file.filename());
    manager.getLibrary().rescan(file.filename().string());
    return record;
  }


  // The ID of the first output of `type` in an import record.
  ren::AssetID findOutput(const json &record, ren::AssetType type) {
    for (const auto &output : record.at("derivedAssets")) {
      if (output.at("type") == json(type)) {
        return ren::parseAssetID(output.at("id"));
      }
    }
    throw std::runtime_error("The import has no output of type " + json(type).get<std::string>());
  }


  class AssetResolve : public rentest::RenderTest {
    void onSetup(rentest::TestContext &ctx) override {
      TemporaryAssets temporary;
      ren::AssetManager manager;
      manager.addFilesystemSource(temporary.root.string());

      // A derived texture becomes a Texture with the size of its pixels.
      auto stone = importTestFile(manager, temporary.root, "assets/test/stone.png");
      auto textureID = findOutput(stone, ren::AssetType::Texture);
      auto texture = manager.resolve<ren::Texture>(textureID);
      auto pixels = ren::readDerivedAsset(manager.getSource(), manager.getLibrary().find(textureID)->path);
      const auto *source = pixels->as<ren::TextureAsset>();
      ctx.check(texture.isSome() && texture.unwrap()->getWidth() == source->width && texture.unwrap()->getHeight() == source->height,
                "a derived texture resolves with the size of its pixels");

      ctx.check(texture.isSome() && texture.unwrap()->getAssetID() == textureID, "a resolved texture has its asset ID");
      ctx.check(ren::Texture::createSinglePixel("code", 0, 0, 0, 255)->getAssetID() == ren::UUID::null, "a texture made in code has no asset ID");

      auto again = manager.resolve<ren::Texture>(textureID);
      ctx.check(again.isSome() && again.unwrap() == texture.unwrap(), "a second resolve gives the same Texture");

      // A derived material resolves through its native content.
      auto cube = importTestFile(manager, temporary.root, "assets/test/meshes/unit_cube.glb");
      auto materialID = findOutput(cube, ren::AssetType::Material);
      auto material = manager.resolve<ren::Material>(materialID);
      ctx.check(material.isSome() && material.unwrap()->getAssetID() == materialID, "a derived material resolves");

      // Bad IDs give None, and a wrong type does not hide the asset from the right type.
      ctx.check(manager.resolve<ren::Texture>(ren::AssetID(ren::UUID::null)).isNone(), "the null ID gives None");
      ctx.check(manager.resolve<ren::Texture>(ren::AssetID::generate()).isNone(), "an unknown ID gives None");
      ctx.check(manager.resolve<ren::Material>(textureID).isNone(), "a texture ID gives no Material");
      ctx.check(manager.resolve<ren::Texture>(textureID).isSome(), "the texture still resolves after a wrong-type request");

      // An object from an asset becomes a reference to the asset. Its resolve() would use the
      // application's AssetManager, which does not have these temporary assets.
      ren::Handle<ren::Texture> fromAsset = texture.unwrap();
      ctx.check(fromAsset.isPersistent() && fromAsset.id() == textureID, "a handle to an asset object keeps only its ID");
      ctx.check(fromAsset == ren::Handle<ren::Texture>(textureID), "a handle to an asset object equals a handle to its ID");
    }
  };


  // A handle to an object that code made keeps the object, and an empty handle resolves to None.
  class AssetHandle : public rentest::RenderTest {
    void onSetup(rentest::TestContext &ctx) override {
      auto made = ren::Texture::createSinglePixel("code", 255, 0, 0, 255);
      ren::Handle<ren::Texture> handle = made;
      auto resolved = handle.resolve();
      ctx.check(handle && !handle.isPersistent() && handle.id() == ren::UUID::null, "a handle to a code object is not persistent");
      ctx.check(resolved.isSome() && resolved.unwrap() == made, "a handle to a code object resolves to the object");

      // The handle keeps the object alive after the code drops its reference.
      std::weak_ptr<ren::Texture> watch = made;
      made.reset();
      ctx.check(!watch.expired(), "a handle keeps its code object alive");

      ren::Handle<ren::Texture> empty;
      ctx.check(!empty && empty.resolve().isNone(), "an empty handle resolves to None");
      ctx.check(!ren::Handle<ren::Texture>(ren::ref<ren::Texture>()), "a handle to a null object is empty");
    }
  };

}  // namespace


REN_RENDER_TEST("assets.resolve", AssetResolve, {.frames = 1, .expect = "No image. The checks are in the log."});
REN_RENDER_TEST("assets.handle", AssetHandle, {.frames = 1, .expect = "No image. The checks are in the log."});
