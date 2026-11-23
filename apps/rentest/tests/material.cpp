// Tests of material field values in the JSON value format, and of the
// .material file format: field values round-trip through the reflected bytes,
// skipped fields stay out of the file, and bad values throw. The tests make no
// viewport, so they have no golden image.

#include "RenderTest.h"

#include <ren/assets/DerivedAsset.h>
#include <ren/assets/Material.h>
#include <ren/assets/NativeAsset.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

namespace {

  using Field = ren::ShaderReflection::MaterialField;
  using ValueKind = ren::ShaderReflection::ValueKind;
  using ScalarKind = ren::ShaderReflection::ScalarKind;


  // A made-up field, with the layout that reflection gives in a scalar-layout buffer.
  Field makeField(ScalarKind scalarKind, u32 scalarSize, ValueKind kind = ValueKind::Scalar, u32 rows = 0, u32 columns = 0) {
    Field field;
    field.name = "value";
    field.typeName = "test";
    field.kind = kind;
    field.scalarKind = scalarKind;
    field.rowCount = rows;
    field.columnCount = columns;
    field.byteSize = scalarSize * std::max(rows, 1u) * std::max(columns, 1u);
    return field;
  }


  // Checks that `value` encodes and decodes to itself.
  void checkRoundTrip(rentest::TestContext &ctx, const Field &field, const json &value) {
    std::vector<u8> bytes(field.byteSize);
    try {
      ren::Material::encodeValue(field, value, bytes.data());
      const auto decoded = ren::Material::decodeValue(field, bytes.data());
      ctx.check(decoded == value, fmt::format("{} round-trips, not {}", value.dump(), decoded.dump()));
    } catch (const std::exception &error) {
      ctx.fail(fmt::format("{} does not encode: {}", value.dump(), error.what()));
    }
  }


  // Checks that `value` does not fit the field.
  void checkRejects(rentest::TestContext &ctx, const Field &field, const json &value) {
    std::vector<u8> bytes(field.byteSize);
    try {
      ren::Material::encodeValue(field, value, bytes.data());
      ctx.fail(fmt::format("{} encodes, but does not fit", value.dump()));
    } catch (const std::invalid_argument &) {
    }
  }


  // Every scalar kind encodes at its natural size, and rejects values out of its range.
  // Most kinds need device features that the test device does not enable, so the fields
  // are made up instead of reflected.
  class MaterialScalarKinds : public rentest::RenderTest {
    void onSetup(rentest::TestContext &ctx) override {
      const auto boolean = makeField(ScalarKind::Bool, 4);
      checkRoundTrip(ctx, boolean, true);
      checkRoundTrip(ctx, boolean, false);
      checkRejects(ctx, boolean, 1);

      const auto int8 = makeField(ScalarKind::Int8, 1);
      checkRoundTrip(ctx, int8, -128);
      checkRoundTrip(ctx, int8, 127);
      checkRejects(ctx, int8, 128);
      checkRejects(ctx, int8, -129);

      const auto uint8 = makeField(ScalarKind::UInt8, 1);
      checkRoundTrip(ctx, uint8, 255);
      checkRejects(ctx, uint8, 256);
      checkRejects(ctx, uint8, -1);

      checkRoundTrip(ctx, makeField(ScalarKind::Int16, 2), -32768);
      checkRejects(ctx, makeField(ScalarKind::Int16, 2), 32768);
      checkRoundTrip(ctx, makeField(ScalarKind::UInt16, 2), 65535);
      checkRejects(ctx, makeField(ScalarKind::UInt16, 2), 65536);

      const auto int32 = makeField(ScalarKind::Int32, 4);
      checkRoundTrip(ctx, int32, std::numeric_limits<i32>::min());
      checkRejects(ctx, int32, i64(std::numeric_limits<i32>::max()) + 1);
      checkRejects(ctx, int32, 1.5);

      // A signed JSON integer from C++ fits an unsigned field if it is not negative.
      const auto uint32 = makeField(ScalarKind::UInt32, 4);
      checkRoundTrip(ctx, uint32, std::numeric_limits<u32>::max());
      checkRoundTrip(ctx, uint32, json::parse("7"));
      checkRoundTrip(ctx, uint32, json(i32(7)));
      checkRejects(ctx, uint32, u64(std::numeric_limits<u32>::max()) + 1);
      checkRejects(ctx, uint32, -1);

      checkRoundTrip(ctx, makeField(ScalarKind::Int64, 8), std::numeric_limits<i64>::min());
      checkRoundTrip(ctx, makeField(ScalarKind::UInt64, 8), std::numeric_limits<u64>::max());
      checkRejects(ctx, makeField(ScalarKind::UInt64, 8), -1);

      // These values are exact in each width.
      checkRoundTrip(ctx, makeField(ScalarKind::Float16, 2), 1.5);
      checkRoundTrip(ctx, makeField(ScalarKind::Float32, 4), 0.25);
      checkRoundTrip(ctx, makeField(ScalarKind::Float64, 8), 0.1);
      checkRejects(ctx, makeField(ScalarKind::Float32, 4), "text");

      // Vectors and matrices step by the scalar size.
      checkRoundTrip(ctx, makeField(ScalarKind::Float16, 2, ValueKind::Vector, 1, 3), json::array({0.5, 1.0, 2.0}));
      checkRoundTrip(ctx, makeField(ScalarKind::Float64, 8, ValueKind::Matrix, 2, 2),
                     json::array({json::array({1.0, 2.0}), json::array({3.0, 4.0})}));
      checkRoundTrip(ctx, makeField(ScalarKind::Int8, 1, ValueKind::Vector, 1, 4), json::array({-1, 0, 1, 2}));

      // A reflected size that does not fit the shape is an error, not a short write.
      auto badLayout = makeField(ScalarKind::Float32, 4, ValueKind::Vector, 1, 3);
      badLayout.byteSize = 8;
      checkRejects(ctx, badLayout, json::array({1.0, 2.0, 3.0}));
    }
  };


  // Every stored field of test/modules/material_fields. The matrices are not
  // symmetric, so a swap of rows and columns changes the result.
  const json kStoredFields = json::parse(R"({
    "scalar":         {"type": "float", "value": 1.5},
    "signedValue":    {"type": "int", "value": -7},
    "unsignedValue":  {"type": "uint", "value": 42},
    "signedLarge":    {"type": "int64_t", "value": -5000000000},
    "vector2":        {"type": "vector<float,2>", "value": [1.0, 2.0]},
    "vector3":        {"type": "vector<float,3>", "value": [3.0, 4.0, 5.0]},
    "color":          {"type": "vector<float,4>", "value": [0.25, 0.5, 0.75, 1.0]},
    "unsignedVector": {"type": "vector<uint,2>", "value": [8, 9]},
    "matrix3":        {"type": "matrix<float,3,3>", "value": [[1.0, 2.0, 3.0], [4.0, 5.0, 6.0], [7.0, 8.0, 9.0]]},
    "matrix4":        {"type": "matrix<float,4,4>", "value": [[1.0, 2.0, 3.0, 4.0], [5.0, 6.0, 7.0, 8.0],
                                                              [9.0, 10.0, 11.0, 12.0], [13.0, 14.0, 15.0, 16.0]]}
  })");


  json makeFile(const json &fields) {
    return {
        {"version", 1},
        {"id", "5f0c2a9e71d4b386"},
        {"surface_shader", "test/modules/material_fields"},
        {"fields", fields},
    };
  }


  const ren::ShaderReflection::MaterialField &findField(const ren::Material &material, std::string_view name) {
    for (const auto &field : material.getSchema()->fields) {
      if (field.name == name) {
        return field;
      }
    }
    throw std::runtime_error(fmt::format("No field '{}'", name));
  }


  float readFloat(const ren::Material &material, u32 offset) {
    float value;
    std::memcpy(&value, material.getCpuMaterialData().data() + offset, sizeof(float));
    return value;
  }


  // Every stored field comes back from serializeNativeAsset() with the same type and value.
  class MaterialRoundTrip : public rentest::RenderTest {
    void onSetup(rentest::TestContext &ctx) override {
      auto material = ren::deserializeNativeAsset<ren::Material>(makeFile(kStoredFields));

      // Record the reflected names and sizes, so that a Slang change shows in the log.
      for (const auto &field : material->getSchema()->fields) {
        ren::dbgln("material field {} : {} offset {} size {} rows {} columns {}", field.name, field.typeName, field.byteOffset, field.byteSize,
                   field.rowCount, field.columnCount);
      }

      const auto saved = ren::serializeNativeAsset(*material);
      ctx.check(saved["id"] == "5f0c2a9e71d4b386", "the id round-trips");
      ctx.check(saved["surface_shader"] == "test/modules/material_fields", "the surface shader round-trips");
      ctx.check(!saved["pso"].contains("program"), "the PSO has no program entry");
      ctx.check(saved["fields"] == kStoredFields, "the fields round-trip: " + saved["fields"].dump());

      // Column 1, row 0 of matrix3 is 4. Its offset tells column-major from row-major.
      const auto &matrix3 = findField(*material, "matrix3");
      const u32 columnStride = matrix3.byteSize / matrix3.columnCount;
      ctx.check(readFloat(*material, matrix3.byteOffset + columnStride) == 4.0f, "matrix3 is column-major in the bytes");

      // A second load of the saved file gives the same bytes.
      auto reloaded = ren::deserializeNativeAsset<ren::Material>(saved);
      ctx.check(reloaded->getCpuMaterialData() == material->getCpuMaterialData(), "a reload gives the same bytes");
    }
  };


  // Fields that the file does not store give a warning and keep their default.
  class MaterialSkippedFields : public rentest::RenderTest {
    void onSetup(rentest::TestContext &ctx) override {
      auto fields = kStoredFields;
      fields["_padding"] = {{"type", "float"}, {"value", 3.0}};
      fields["texture"] = {{"type", "BindlessTexture"}, {"value", "a41e93c07b2d5f18"}};
      fields["removed"] = {{"type", "float"}, {"value", 1.0}};
      fields["scalar"]["type"] = "int";  // A retyped field keeps its default.

      auto material = ren::deserializeNativeAsset<ren::Material>(makeFile(fields));
      const auto saved = ren::serializeNativeAsset(*material)["fields"];
      ctx.check(!saved.contains("_padding"), "'_padding' is not saved");
      ctx.check(!saved.contains("texture"), "'texture' is not saved");
      ctx.check(!saved.contains("removed"), "'removed' is not saved");
      ctx.check(saved["scalar"]["value"] == 0.0, "the retyped 'scalar' keeps its default");
      ctx.check(saved["color"] == kStoredFields["color"], "the other fields load");
    }
  };


  // A value that does not fit its field throws, and a failed write changes no bytes.
  class MaterialBadValues : public rentest::RenderTest {
    void onSetup(rentest::TestContext &ctx) override {
      auto material = ren::deserializeNativeAsset<ren::Material>(makeFile(kStoredFields));
      const auto before = material->getCpuMaterialData();

      auto throws = [&](const char *name, const json &value) {
        try {
          material->setValue(name, value);
          ctx.fail(fmt::format("setValue('{}', {}) does not throw", name, value.dump()));
        } catch (const std::invalid_argument &) {
        }
      };
      throws("vector3", json::array({1.0, 2.0}));
      throws("matrix3", json::array({json::array({1.0, 2.0, 3.0})}));
      throws("matrix3", json::array({json::array({1.0, 2.0, 3.0}), json::array({4.0, 5.0}), json::array({6.0, 7.0, 8.0})}));
      throws("unsignedValue", -1);
      throws("signedValue", 1.5);
      throws("scalar", "text");
      throws("_padding", 1.0);
      throws("texture", 0);
      throws("missing", 1.0);
      ctx.check(material->getCpuMaterialData() == before, "failed writes change no bytes");
    }
  };


  // The example file in the assets folder loads against the PBR surface.
  class MaterialExampleFile : public rentest::RenderTest {
    void onSetup(rentest::TestContext &ctx) override {
      std::ifstream file("assets/example.material");
      if (!file) {
        ctx.skip("assets/example.material is not in the working folder");
        return;
      }

      auto material = ren::deserializeNativeAsset<ren::Material>(json::parse(file));
      ctx.check(material->getValue("roughness") == 1.0, "roughness loads from the example");
      ctx.check(ren::serializeNativeAsset(*material)["surface_shader"] == "ren/core/pbr", "the example uses the PBR surface");
    }
  };


  // The id and version are checked before the content, and the registry finds .material files.
  class MaterialIdAndVersion : public rentest::RenderTest {
    void onSetup(rentest::TestContext &ctx) override {
      auto throws = [&](const char *what, json file) {
        try {
          ren::deserializeNativeAsset<ren::Material>(file);
          ctx.fail(fmt::format("a file with {} loads", what));
        } catch (const std::runtime_error &) {
        }
      };
      const auto file = makeFile(kStoredFields);
      auto with = [&](const char *key, const json &value) {
        auto changed = file;
        changed[key] = value;
        return changed;
      };
      auto without = file;
      without.erase("id");

      throws("another version", with("version", 2));
      throws("no id", without);
      throws("a bad id", with("id", "xyz"));
      throws("a null id", with("id", "null"));

      const auto *info = ren::findNativeTypeForPath("folder/name.material");
      ctx.check(info != nullptr && info->type == ren::AssetType::Material, "the registry finds .material files");
      ctx.check(ren::findNativeTypeForPath("folder/name.png") == nullptr, "the registry does not find .png files");
    }
  };


  // A derived bundle of material content reads back through the bundle registry, and makes the
  // same material as a native file with that content.
  class MaterialDerivedBundle : public rentest::RenderTest {
    void onSetup(rentest::TestContext &ctx) override {
      const auto root = std::filesystem::temp_directory_path() / ("ren-material-test-" + ren::AssetID::generate().to_string());
      std::filesystem::create_directories(root);
      ren::FilesystemAssetSource source(root);

      auto content = makeFile(kStoredFields);
      content.erase("version");
      content.erase("id");
      ren::NativeContentAsset written(ren::AssetType::Material, content);
      written.id = ren::AssetID::generate();
      written.sourceAsset = ren::AssetID::generate();
      written.name = "derived material";

      const auto path = ren::derivedPath(ren::derivedBundlePath(written.id));
      ren::writeDerivedAsset(source, path, written);
      auto read = std::dynamic_pointer_cast<ren::NativeContentAsset>(ren::readDerivedAsset(source, path));
      std::filesystem::remove_all(root);

      if (!read) {
        ctx.fail("the bundle does not read back as a NativeContentAsset");
        return;
      }
      ctx.check(read->id == written.id && read->sourceAsset == written.sourceAsset, "the bundle keeps the identity");
      ctx.check(read->content == content, "the bundle keeps the content");

      auto material = ren::deserializeNativeAsset<ren::Material>(read->serializeNativeFile());
      ctx.check(material->getAssetID() == written.id, "the material has the bundle ID");
      ctx.check(ren::serializeNativeAsset(*material)["fields"] == kStoredFields, "the material has the bundle fields");

      // The content must not hold the keys that the bundle header and the type supply.
      auto withID = content;
      withID["id"] = written.id.to_string();
      try {
        ren::NativeContentAsset(ren::AssetType::Material, withID).validate();
        ctx.fail("content with an id passes validation");
      } catch (const std::runtime_error &) {
      }
    }
  };

}  // namespace


REN_RENDER_TEST("material.scalar_kinds", MaterialScalarKinds, {.frames = 1, .expect = "No image. The checks are in the log."});
REN_RENDER_TEST("material.round_trip", MaterialRoundTrip, {.frames = 1, .expect = "No image. The checks are in the log."});
REN_RENDER_TEST("material.skipped_fields", MaterialSkippedFields, {.frames = 1, .expect = "No image. The checks are in the log."});
REN_RENDER_TEST("material.bad_values", MaterialBadValues, {.frames = 1, .expect = "No image. The checks are in the log."});
REN_RENDER_TEST("material.example_file", MaterialExampleFile, {.frames = 1, .expect = "No image. The checks are in the log."});
REN_RENDER_TEST("material.id_and_version", MaterialIdAndVersion, {.frames = 1, .expect = "No image. The checks are in the log."});
REN_RENDER_TEST("material.derived_bundle", MaterialDerivedBundle, {.frames = 1, .expect = "No image. The checks are in the log."});
