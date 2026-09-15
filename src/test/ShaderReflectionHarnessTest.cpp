#include "ShaderReflectionTestHarness.h"

#include <gtest/gtest.h>

namespace ren::test {

  TEST_F(ShaderReflectionTest, MixedParameterBlockMatchesPhysicalSpirv) {
    constexpr std::string_view source = R"slang(
      struct Params
      {
        float scale;
        RWStructuredBuffer<float> output;
      }

      ParameterBlock<Params> params;

      [shader("compute")]
      [numthreads(1, 1, 1)]
      void main(uint3 id : SV_DispatchThreadID)
      {
        params.output[id.x] = params.scale;
      }
    )slang";

    ReflectedSlangCase reflected;
    ASSERT_NO_THROW(
        reflected = reflectSource(source, {}, "mixed_parameter_block"));
    expectBindingsConsistent(reflected);
  }

  // Binding indices in Slang reflection are relative to the enclosing scope and
  // must be accumulated along the access path. A resource nested inside a
  // struct must not collide with a sibling resource of the struct.
  TEST_F(ShaderReflectionTest, NestedStructResourcesAccumulateBindingOffsets) {
    constexpr std::string_view source = R"slang(
      Texture2D t0;

      struct Maps
      {
        Texture2D a;
        Texture2D b;
      }

      Maps maps;
      RWStructuredBuffer<float> output;

      [shader("compute")]
      [numthreads(1, 1, 1)]
      void main(uint3 id : SV_DispatchThreadID)
      {
        output[id.x] = t0.Load(int3(0, 0, 0)).x
                     + maps.a.Load(int3(0, 0, 0)).y
                     + maps.b.Load(int3(0, 0, 0)).z;
      }
    )slang";

    ReflectedSlangCase reflected;
    ASSERT_NO_THROW(reflected = reflectSource(source, {}, "nested_struct_resources"));
    expectBindingsConsistent(reflected);
  }

  // A bare global uniform makes slang wrap the entire global scope in an
  // implicit constant buffer ($Globals). The reflection must surface that
  // buffer and must not silently drop the other global parameters.
  TEST_F(ShaderReflectionTest, GlobalUniformsWrapInImplicitConstantBuffer) {
    constexpr std::string_view source = R"slang(
      uniform float exposure;
      RWStructuredBuffer<float> output;

      [shader("compute")]
      [numthreads(1, 1, 1)]
      void main(uint3 id : SV_DispatchThreadID)
      {
        output[id.x] = exposure;
      }
    )slang";

    ReflectedSlangCase reflected;
    ASSERT_NO_THROW(reflected = reflectSource(source, {}, "global_uniforms"));
    expectBindingsConsistent(reflected);
  }

  // Resources nested one struct deeper inside a ParameterBlock must still land
  // on their absolute binding within the block's descriptor set (after the
  // implicit constant buffer at binding 0).
  TEST_F(ShaderReflectionTest, NestedStructInsideParameterBlock) {
    constexpr std::string_view source = R"slang(
      struct Inner
      {
        Texture2D tex;
      }

      struct Params
      {
        float scale;
        Inner inner;
        RWStructuredBuffer<float> output;
      }

      ParameterBlock<Params> params;

      [shader("compute")]
      [numthreads(1, 1, 1)]
      void main(uint3 id : SV_DispatchThreadID)
      {
        params.output[id.x] = params.scale * params.inner.tex.Load(int3(0, 0, 0)).x;
      }
    )slang";

    ReflectedSlangCase reflected;
    ASSERT_NO_THROW(reflected = reflectSource(source, {}, "nested_struct_in_parameter_block"));
    expectBindingsConsistent(reflected);
  }

  // A sized resource array is a single Vulkan binding with descriptorCount N,
  // and its element descriptor type must survive into the engine binding.
  TEST_F(ShaderReflectionTest, SizedResourceArrayReportsCountAndElementType) {
    constexpr std::string_view source = R"slang(
      Texture2D textures[4];
      RWStructuredBuffer<float> output;

      [shader("compute")]
      [numthreads(1, 1, 1)]
      void main(uint3 id : SV_DispatchThreadID)
      {
        output[id.x] = textures[id.x % 4].Load(int3(0, 0, 0)).x;
      }
    )slang";

    ReflectedSlangCase reflected;
    ASSERT_NO_THROW(reflected = reflectSource(source, {}, "sized_resource_array"));
    expectBindingsConsistent(reflected);
  }

  // Unbounded arrays report SLANG_UNBOUNDED_SIZE as their element count; the
  // parser must not attempt to enumerate the elements.
  TEST_F(ShaderReflectionTest, UnboundedResourceArrayDoesNotHang) {
    constexpr std::string_view source = R"slang(
      Texture2D textures[];
      RWStructuredBuffer<float> output;

      [shader("compute")]
      [numthreads(1, 1, 1)]
      void main(uint3 id : SV_DispatchThreadID)
      {
        output[id.x] = textures[NonUniformResourceIndex(id.x)].Load(int3(0, 0, 0)).x;
      }
    )slang";

    ReflectedSlangCase reflected;
    ASSERT_NO_THROW(reflected = reflectSource(source, {}, "unbounded_resource_array"));
    expectBindingsConsistent(reflected);
  }

  TEST_F(ShaderReflectionTest, OptimizedProgramMayPruneUnusedBindings) {
    constexpr std::string_view source = R"slang(
      struct Params
      {
        float scale;
        RWStructuredBuffer<float> output;
        Texture2D unusedTexture;
      }

      ParameterBlock<Params> params;

      [shader("compute")]
      [numthreads(1, 1, 1)]
      void main(uint3 id : SV_DispatchThreadID)
      {
        params.output[id.x] = params.scale;
      }
    )slang";

    ReflectedSlangCase reflected;
    ASSERT_NO_THROW(reflected = reflectSource(
        source,
        {.optimization = SLANG_OPTIMIZATION_LEVEL_MAXIMAL},
        "optimized_unused_binding"));
    expectBindingsConsistent(
        reflected, UnusedResourcePolicy::AllowPrunedEngineBindings);
  }

  TEST_F(ShaderReflectionTest, ExplicitImageAliasesShareOnePhysicalBinding) {
    constexpr std::string_view source = R"slang(
      [[vk::binding(0, 1)]] Texture2D<float4> images2D[];
      [[vk::binding(0, 1)]] TextureCube<float4> imagesCube[];
      [[vk::binding(0, 2)]] SamplerState samplers[];

      [shader("fragment")]
      float4 main(float3 uv : TEXCOORD) : SV_Target
      {
        uint index = NonUniformResourceIndex(1);
        return images2D[index].Sample(samplers[0], uv.xy) +
            imagesCube[index].Sample(samplers[0], uv);
      }
    )slang";

    ReflectedSlangCase reflected;
    ASSERT_NO_THROW(
        reflected = reflectSource(source, {}, "explicit_image_aliases"));

    ASSERT_EQ(reflected.physical.bindings.size(), 2);
    EXPECT_EQ(
        reflected.physical.bindings.at({1, 0}).type,
        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    EXPECT_EQ(
        reflected.physical.bindings.at({2, 0}).type,
        VK_DESCRIPTOR_TYPE_SAMPLER);
    EXPECT_EQ(reflected.physical.bindings.at({1, 0}).count, 0);
    EXPECT_EQ(reflected.physical.bindings.at({2, 0}).count, 0);
  }

  TEST_F(ShaderReflectionTest, NamedPushConstantsPreserveRecursiveFieldLocations) {
    constexpr std::string_view source = R"slang(
      struct Transform
      {
        float2 offset;
        float scale;
      };

      struct DrawConstants
      {
        Transform transform;
        uint materialIndex;
        uint pattern;
      };

      [[vk::push_constant]]
      ConstantBuffer<DrawConstants> draw;

      struct Varying
      {
        float4 position : SV_Position;
      };

      [shader("vertex")]
      Varying vertexMain(uint id : SV_VertexID)
      {
        Varying output;
        output.position = float4(
            draw.transform.offset * draw.transform.scale,
            float(draw.materialIndex), 1.0f);
        return output;
      }

      [shader("fragment")]
      float4 fragmentMain(Varying input) : SV_Target
      {
        return float4(input.position.xy, draw.transform.scale,
            float(draw.materialIndex));
      }
    )slang";

    ReflectedSlangCase reflected;
    ASSERT_NO_THROW(
        reflected = reflectSource(
            source,
            {.vulkanEmitReflection = false},
            "recursive_push_constant"));

    auto* root = reflected.slangReflection->getRoot();
    ASSERT_NE(root, nullptr);
    auto* draw = static_cast<ShaderReflection::Node*>(nullptr);
    for (auto* member : root->members) {
      if (member != nullptr && member->name == "draw") {
        draw = member;
        break;
      }
    }
    ASSERT_NE(draw, nullptr);
    EXPECT_EQ(draw->name, "draw");
    EXPECT_EQ(draw->type.type, ShaderReflection::Type::PushConstant);
    EXPECT_TRUE(draw->location.pushConstant);
    ASSERT_TRUE(draw->location.byteOffset);
    ASSERT_TRUE(draw->location.byteSize);
    EXPECT_EQ(*draw->location.byteOffset, 0);
    EXPECT_EQ(*draw->location.byteSize, 24);

    ASSERT_EQ(draw->members.size(), 3);
    auto* transform = draw->members[0];
    ASSERT_NE(transform, nullptr);
    EXPECT_EQ(transform->name, "transform");
    EXPECT_TRUE(transform->location.pushConstant);
    ASSERT_TRUE(transform->location.byteOffset);
    ASSERT_TRUE(transform->location.byteSize);
    EXPECT_EQ(*transform->location.byteOffset, 0);
    EXPECT_EQ(*transform->location.byteSize, 16);

    ASSERT_EQ(transform->members.size(), 2);
    EXPECT_EQ(transform->members[0]->name, "offset");
    EXPECT_EQ(*transform->members[0]->location.byteOffset, 0);
    EXPECT_EQ(*transform->members[0]->location.byteSize, 8);
    EXPECT_EQ(transform->members[1]->name, "scale");
    EXPECT_EQ(*transform->members[1]->location.byteOffset, 8);
    EXPECT_EQ(*transform->members[1]->location.byteSize, 4);

    auto* materialIndex = draw->members[1];
    ASSERT_NE(materialIndex, nullptr);
    EXPECT_EQ(materialIndex->name, "materialIndex");
    EXPECT_EQ(*materialIndex->location.byteOffset, 16);
    EXPECT_EQ(*materialIndex->location.byteSize, 4);
    EXPECT_TRUE(reflected.slangReflection->getMaterialSchema().isNone());
  }

  TEST_F(ShaderReflectionTest, DrawParamsReflectsPointedToMaterialSchema) {
    constexpr std::string_view source = R"slang(
      [__AttributeUsage(_AttributeTargets.Var)]
      struct ColorAttribute {};

      [__AttributeUsage(_AttributeTargets.Var)]
      struct DefaultAttribute { float value; };

      [__AttributeUsage(_AttributeTargets.Var)]
      struct SourceAttribute { string path; };

      [__AttributeUsage(_AttributeTargets.Var)]
      struct OrderAttribute { int value; };

      struct ObjectData
      {
        float4x4 modelMatrix;
        float4x4 normalMatrix;
        uint objectId;
        uint _padding[3];
      }

      namespace ren
      {
        [__AttributeUsage(_AttributeTargets.Struct)]
        struct MaterialDrawParamsAttribute {};

        struct InstanceRecord
        {
          uint objectIndex;
          uint materialIndex;
        }

        [MaterialDrawParams]
        struct DrawParams<MaterialT>
        {
          InstanceRecord* instances;
          ObjectData* objects;
          MaterialT* materials;
        }
      }

      struct TextureHandle
      {
        uint packed;
      }

      struct Material
      {
        [Source("default-white.png")]
        [Order(2)]
        TextureHandle textureIndex;
        [Color]
        [Default(0.5)]
        float4 color;
      }

      [[vk::push_constant]]
      ren.DrawParams<Material> arbitraryName;

      [shader("compute")]
      [numthreads(1, 1, 1)]
      void main() {}
    )slang";

    ReflectedSlangCase reflected;
    ASSERT_NO_THROW(reflected = reflectSource(
        source, {.vulkanEmitReflection = false}, "draw_params_material"));

    const auto& schemaOption = reflected.slangReflection->getMaterialSchema();
    ASSERT_TRUE(schemaOption.isSome());
    const auto& schema = schemaOption.unwrap();
    EXPECT_EQ(schema.pushConstantName, "arbitraryName");
    EXPECT_EQ(schema.pushConstantByteSize, 24);
    EXPECT_EQ(schema.instancePointerOffset, 0);
    EXPECT_EQ(schema.objectPointerOffset, 8);
    EXPECT_EQ(schema.materialPointerOffset, 16);

    auto* root = reflected.slangReflection->getRoot();
    ASSERT_NE(root, nullptr);
    auto* draw = static_cast<ShaderReflection::Node*>(nullptr);
    for (auto* member : root->members) {
      if (member != nullptr && member->name == "arbitraryName") {
        draw = member;
        break;
      }
    }
    ASSERT_NE(draw, nullptr);
    ASSERT_EQ(draw->members.size(), 3);
    EXPECT_EQ(draw->members[0]->name, "instances");
    EXPECT_EQ(*draw->members[0]->location.byteOffset, 0);
    EXPECT_EQ(draw->members[1]->name, "objects");
    EXPECT_EQ(*draw->members[1]->location.byteOffset, 8);
    EXPECT_EQ(draw->members[2]->name, "materials");
    EXPECT_EQ(*draw->members[2]->location.byteOffset, 16);

    EXPECT_EQ(schema.typeName, "Material");
    EXPECT_EQ(schema.byteSize, 20);
    EXPECT_EQ(schema.alignment, 4);
    ASSERT_EQ(schema.fields.size(), 2);

    const auto& texture = schema.fields[0];
    EXPECT_EQ(texture.name, "textureIndex");
    EXPECT_EQ(texture.typeName, "TextureHandle");
    EXPECT_EQ(texture.kind, ShaderReflection::ValueKind::Struct);
    EXPECT_EQ(texture.byteOffset, 0);
    EXPECT_EQ(texture.byteSize, 4);
    EXPECT_EQ(texture.alignment, 4);
    ASSERT_EQ(texture.fields.size(), 1);
    EXPECT_EQ(texture.fields[0].name, "packed");
    EXPECT_EQ(texture.fields[0].scalarKind, ShaderReflection::ScalarKind::UInt32);
    ASSERT_EQ(texture.attributes.size(), 2);
    EXPECT_EQ(texture.attributes[0].name, "Source");
    ASSERT_EQ(texture.attributes[0].arguments.size(), 1);
    EXPECT_EQ(texture.attributes[0].arguments[0], "default-white.png");
    EXPECT_EQ(texture.attributes[1].name, "Order");
    ASSERT_EQ(texture.attributes[1].arguments.size(), 1);
    EXPECT_EQ(texture.attributes[1].arguments[0], 2);

    const auto& color = schema.fields[1];
    EXPECT_EQ(color.name, "color");
    EXPECT_EQ(color.kind, ShaderReflection::ValueKind::Vector);
    EXPECT_EQ(color.scalarKind, ShaderReflection::ScalarKind::Float32);
    EXPECT_EQ(color.byteOffset, 4);
    EXPECT_EQ(color.byteSize, 16);
    EXPECT_EQ(color.alignment, 4);
    EXPECT_EQ(color.rowCount * color.columnCount, 4);
    ASSERT_EQ(color.attributes.size(), 2);
    EXPECT_EQ(color.attributes[0].name, "Color");
    EXPECT_TRUE(color.attributes[0].arguments.empty());
    EXPECT_EQ(color.attributes[1].name, "Default");
    ASSERT_EQ(color.attributes[1].arguments.size(), 1);
    EXPECT_EQ(color.attributes[1].arguments[0], 0.5f);
  }

}  // namespace ren::test
