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
};

// ... Which is provided in a constant buffer per frame.
[[vk::binding(0, 0)]]
public ConstantBuffer<FrameGlobals> frameGlobals;

// REN is a bindless engine, and as such we provide a set of descriptor heaps
// for images and samplers. We put them in the same descriptor set
// NOTE: vk::binding is backwards, it is vk::binding(index, set), not (set, index)

// Descriptor set 1 has ImageViews aliased
[[vk::binding(0, 1)]]
Texture2D<float4> __ren_sampledImages2D[];
// [[vk::binding(0, 1)]]
// TextureCube<float4> __ren_sampledImagesCube[];

// Set 2 has the SamplerStates.
[[vk::binding(0, 2)]]
SamplerState __ren_samplers[];

// We then provide a set of helper functions to access those descriptor heaps
// without having to deal with the NonUniformResourceIndex() stuff in the shader code.
public Texture2D<float4> getTexture2D(uint index) { return __ren_sampledImages2D[NonUniformResourceIndex(index)]; }

// public TextureCube<float4> getTextureCube(uint index) {
//   return __ren_sampledImagesCube[NonUniformResourceIndex(index)];
// }

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
