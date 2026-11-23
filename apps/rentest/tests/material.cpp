// Tests of material field values in the JSON value format.

#include "RenderTest.h"

#include <ren/assets/Material.h>

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

}  // namespace


REN_RENDER_TEST("material.scalar_kinds", MaterialScalarKinds, {.frames = 1, .expect = "No image. The checks are in the log."});
