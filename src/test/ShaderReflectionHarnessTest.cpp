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

}  // namespace ren::test
