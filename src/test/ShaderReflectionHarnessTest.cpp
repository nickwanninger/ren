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

  TEST_F(ShaderReflectionTest, DescriptorHandlesUseTypedGlobalHeapWithoutMutableDescriptors) {
    constexpr std::string_view source = R"slang(
      export T getDescriptorFromHandle<T>(DescriptorHandle<T> handle)
          where T : IOpaqueDescriptor
      {
        return defaultGetDescriptorFromHandle(
            handle, BindlessDescriptorOptions.None);
      }

      struct PushConstants
      {
        Texture2D<float4>.Handle source;
        RWTexture2D<float4>.Handle destination;
        SamplerState.Handle sampler;
      };

      [[vk::push_constant]]
      ConstantBuffer<PushConstants> pushConstants;

      [shader("compute")]
      [numthreads(1, 1, 1)]
      void main(uint3 id : SV_DispatchThreadID)
      {
        float4 value = pushConstants.source.SampleLevel(
            pushConstants.sampler, float2(0.5f), 0.0f);
        pushConstants.destination[id.xy] = value;
      }
    )slang";

    ReflectedSlangCase reflected;
    ASSERT_NO_THROW(
        reflected = reflectSource(source, {}, "descriptor_handle_heap"));

    ASSERT_EQ(reflected.physical.bindings.size(), 3);
    EXPECT_EQ(
        reflected.physical.bindings.at({1, 0}).type,
        VK_DESCRIPTOR_TYPE_SAMPLER);
    EXPECT_EQ(
        reflected.physical.bindings.at({1, 2}).type,
        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    EXPECT_EQ(
        reflected.physical.bindings.at({1, 3}).type,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    EXPECT_EQ(reflected.physical.bindings.at({1, 0}).count, 0);
    EXPECT_EQ(reflected.physical.bindings.at({1, 2}).count, 0);
    EXPECT_EQ(reflected.physical.bindings.at({1, 3}).count, 0);
  }

  TEST_F(ShaderReflectionTest, NamedPushConstantsPreserveRecursiveFieldLocations) {
    constexpr std::string_view source = R"slang(
      export T getDescriptorFromHandle<T>(DescriptorHandle<T> handle)
          where T : IOpaqueDescriptor
      {
        return defaultGetDescriptorFromHandle(
            handle, BindlessDescriptorOptions.None);
      }

      struct Transform
      {
        float2 offset;
        float scale;
      };

      struct DrawConstants
      {
        Transform transform;
        uint materialIndex;
        Texture2D<float4>.Handle pattern;
        SamplerState.Handle sampler;
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
        return draw.pattern.Sample(draw.sampler, input.position.xy) + float4(
            input.position.xy, draw.transform.scale,
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
    EXPECT_EQ(*draw->location.byteSize, 40);

    ASSERT_EQ(draw->members.size(), 4);
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
  }

}  // namespace ren::test
