// The Core vulkan shader ABI. All shaders ought to import this module to
// maintain correctness and compatibility with the renderer.  The main thing
// that this module provides is the FrameGlobals constnat bufer, as well as
// the set of descriptor heaps.

// The per-frame global data that is provided to all shaders.
public struct FrameGlobals {
  public float time;
  public float deltaTime;
  public uint frameNumber;
  public uint _padding;
  public float2 renderSize;
  public float2 inverseRenderSize;
  public float4x4 viewMatrix;
  public float4x4 projMatrix;
};

// ... Which is provided in a constant buffer per frame.
[[vk::binding(0, 0)]]
public ConstantBuffer<FrameGlobals> frameGlobals;

// REN is a bindless engine, and as such we provide a set of descriptor heaps
// for images and samplers. We put them in the same descriptor set

// Descriptor set 1 has ImageViews aliased
[[vk::binding(0, 1)]]
Texture2D<float4> __ren_sampledImages2D[];

// Set 2 has the SamplerStates.
[[vk::binding(0, 2)]]
SamplerState __ren_samplers[];

public Texture2D<float4> getTexture2D(uint index) { return __ren_sampledImages2D[NonUniformResourceIndex(index)]; }
public SamplerState getSampler(uint index) { return __ren_samplers[NonUniformResourceIndex(index)]; }

// Importantly, REN doesn't expose CombinedImageSamplers to the shader. Instead
// we provide a small struct that packs the image sampler indices into a single
// uint, and provides a Sample() function to sample the texture.
public struct TextureHandle {
  public uint packed;
  public uint imageIndex() { return packed >> 8; }
  public uint samplerIndex() { return packed & 0xff; }
  public bool isValid() { return imageIndex() != 0; }
  public float4 Sample(float2 uv) {
    if (!isValid())
      return float4(1.0, 0.0, 1.0, 1.0);
    return getTexture2D(imageIndex()).Sample(getSampler(samplerIndex()), uv);
  }
  public float4 SampleLevel(float2 uv, float lod) {
    if (!isValid())
      return float4(1.0, 0.0, 1.0, 1.0);
    return getTexture2D(imageIndex()).SampleLevel(getSampler(samplerIndex()), uv, lod);
  }
}

// The standard vertex structure that is used in the MegaMeshBuffer and other places.
public struct Vertex {
  [[vk::location(0)]]
  public float3 position;
  [[vk::location(1)]]
  public float3 normal;
  [[vk::location(2)]]
  public float2 texCoord;
  [[vk::location(3)]]
  public float3 tangent;
  [[vk::location(4)]]
  public float3 bitangent;

  // TODO: add a function to transform the vertex by a MVP matrix.
}

public struct GBufferOutput {
  [[vk::location(0)]]
  public float4 albedo;
  [[vk::location(1)]]
  public float4 normal;
  [[vk::location(2)]]
  public float4 metallicRoughness;
}

public float4 transformToClipSpace(float3 position, float4x4 model, float4x4 view, float4x4 proj) {
  float4 worldPos = mul(model, float4(position, 1.0f));
  float4 viewPos = mul(view, worldPos);
  return mul(proj, viewPos);
}

public struct ObjectData {
  public float4x4 modelMatrix;
  public float4x4 normalMatrix;
  public uint objectId;
  public uint _padding[3];

  public float4 transformPositionToClipSpace(float3 position) {
    return transformToClipSpace(position, modelMatrix, frameGlobals.viewMatrix, frameGlobals.projMatrix);
  }

  public float3 transformNormal(float3 normal) { return mul(float3x3(normalMatrix), normal); }
}

public struct InstanceRecord {
  public uint objectIndex;
  public uint materialIndex;
}

[__AttributeUsage(_AttributeTargets.Struct)]
struct MaterialDrawParamsAttribute {}

// The following struct is used to pass data to the shader for a normal graphics
// draw call that uses a material to render an object. The contract is that the
// shader will have this defined like the following somewhere at the top of its
// code:
// struct Material { ... }
// [[vk::push_constant]] uniform MaterialRoot<Material> draw;
[MaterialDrawParams]
public struct MaterialRoot<MaterialT> {

  InstanceRecord *instances;
  ObjectData *objects;
  MaterialT *materials;

  // Pointer arithmetic rather than &array[i]: the latter lowers to a value
  // load (and then fails to convert back to a pointer) in Slang's Metal path.
  [ForceInline] public ObjectData *getObject(uint drawIndex) { return objects + instances[drawIndex].objectIndex; }
  [ForceInline] public MaterialT *getMaterial(uint drawIndex) { return materials + instances[drawIndex].materialIndex; }
}

[__AttributeUsage(_AttributeTargets.Var)]
struct DefaultAttribute {
  float value;
}

[__AttributeUsage(_AttributeTargets.Var)]
struct DisplayNameAttribute {
  string value;
}

[__AttributeUsage(_AttributeTargets.Var)]
struct SourceAttribute {
  string path;
}

[__AttributeUsage(_AttributeTargets.Var)]
struct FloatRangeAttribute {
  float min, max;
}
