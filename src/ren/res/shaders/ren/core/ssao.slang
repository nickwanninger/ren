// Screen-space ambient occlusion as a fullscreen surface. Link it with
// ren/pass/fullscreen. The output is the ambient visibility in [0, 1]:
// 1 is no occlusion.
//
// Based on https://github.com/shabaazxh/Real-Time-Ambient-Occlusion.

import ren.core;
import ren.surface;

[[vk::push_constant]]
cbuffer root {
  // The G-buffer depth (D32, [0, 1], 1 is the far plane).
  TextureHandle depth;
  // The size of the SSAO target in pixels. It selects the kernel rotation of
  // each pixel.
  float2 targetSize;
  // The size of the depth image. It can be larger than the target.
  float2 depthSize;
  // The view-space radius of the sample hemisphere.
  float radius;
  // Depth bias against self-occlusion on flat surfaces.
  float bias;
  // The number of kernel samples.
  uint sampleCount;
}

export struct Surface : IFullscreenSurface = SSAO;

// A fixed kernel sample in the +Z hemisphere. The directions are spherical
// Fibonacci points, which spread evenly. The lengths use a second, unrelated
// sequence and are scaled so that more samples are near the center.
float3 kernelSample(uint i, uint count) {
  const float goldenAngle = 2.39996323;
  float z = (float(i) + 0.5) / float(count);
  float r = sqrt(1.0 - z * z);
  float phi = float(i) * goldenAngle;
  float3 dir = float3(r * cos(phi), r * sin(phi), z);

  float u = frac(float(i) * 0.7548776662 + 0.5);
  return dir * lerp(0.1, 1.0, u * u);
}

// The kernel rotation of a pixel, in [0, 2 pi). The 16 rotations repeat in
// 4x4 pixel tiles, in Bayer order, so that neighbors differ as much as
// possible. A blur over 4x4 pixels (ssao_blur.slang) then averages all 16
// rotations, which gives 16 times the samples without the blotches of a
// larger noise pattern.
float kernelRotation(float2 uv) {
  static const uint kBayer4x4[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
  uint2 pixel = uint2(uv * targetSize);
  uint index = kBayer4x4[(pixel.y & 3u) * 4u + (pixel.x & 3u)];
  return (float(index) + 0.5) / 16.0 * 6.28318531;
}

// The UV of the depth texel that an SSAO pixel uses. When the target is
// smaller than the depth image, the pixel center is on a corner between
// depth texels, where nearest sampling can pick any of them, and the choice
// flickers. So use the top-left texel of the pixel's block, from the integer
// pixel index. ssao_blur.slang uses the same texel for its guides.
float2 depthTexelUV(float2 uv) {
  float2 pixel = floor(uv * targetSize);
  float2 texel = floor(pixel * (depthSize / targetSize));
  return (texel + 0.5) / depthSize;
}

// The view-space normal of the geometry at a pixel, from the depth of its
// neighbors. It does not include normal-map detail: a normal-mapped normal
// tilts the hemisphere into the real surface, and the samples there then
// count as occluded. On each axis the neighbor nearer in depth is used, so
// that a neighbor across a silhouette does not bend the normal.
float3 viewNormalFromDepth(float2 uv, float3 P) {
  float2 texel = 1.0 / depthSize;
  float3 left = viewPositionFromDepth(uv - float2(texel.x, 0.0), depth.SampleLevel(uv - float2(texel.x, 0.0), 0.0).r);
  float3 right = viewPositionFromDepth(uv + float2(texel.x, 0.0), depth.SampleLevel(uv + float2(texel.x, 0.0), 0.0).r);
  float3 up = viewPositionFromDepth(uv - float2(0.0, texel.y), depth.SampleLevel(uv - float2(0.0, texel.y), 0.0).r);
  float3 down = viewPositionFromDepth(uv + float2(0.0, texel.y), depth.SampleLevel(uv + float2(0.0, texel.y), 0.0).r);

  float3 dx = abs(right.z - P.z) < abs(P.z - left.z) ? right - P : P - left;
  float3 dy = abs(down.z - P.z) < abs(P.z - up.z) ? down - P : P - up;
  float3 N = normalize(cross(dx, dy));

  // Make the normal face the camera, which is at the origin.
  return dot(N, -P) < 0.0 ? -N : N;
}

struct SSAO : IFullscreenSurface {
  static float4 shade(FullscreenVarying input) {
    // SampleLevel everywhere: the loop below has non-uniform control flow,
    // where implicit derivatives are not defined.
    float2 depthUV = depthTexelUV(input.uv);
    float d = depth.SampleLevel(depthUV, 0.0).r;

    // Nothing was drawn here (the sky), so there is no occlusion.
    if (d >= 1.0) {
      return float4(1.0);
    }

    float3 P = viewPositionFromDepth(depthUV, d);

    float3 N = viewNormalFromDepth(depthUV, P);

    // A tangent frame around N, rotated about N by the pixel's rotation.
    float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 T0 = normalize(cross(up, N));
    float3 B0 = cross(N, T0);
    float angle = kernelRotation(input.uv);
    float3 T = cos(angle) * T0 + sin(angle) * B0;
    float3 B = cross(N, T);

    float occlusion = 0.0;
    for (uint i = 0; i < sampleCount; ++i) {
      float3 k = kernelSample(i, sampleCount);
      float3 samplePos = P + (T * k.x + B * k.y + N * k.z) * radius;

      float2 sampleUV = uvFromViewPosition(samplePos);
      if (any(sampleUV < 0.0) || any(sampleUV > 1.0)) {
        continue;
      }

      float3 occluder = viewPositionFromDepth(sampleUV, depth.SampleLevel(sampleUV, 0.0).r);

      // The camera looks down -Z, so a larger z is nearer. The sample is
      // occluded when the surface at its pixel is in front of it.
      // The occluder counts less with its distance from P, and not at all
      // from the radius out. A hard cut at the radius lets an object in front
      // of a wall darken the wall behind it (a halo).
      float dist = length(occluder - P) / radius;
      float falloff = saturate(1.0 - dist * dist);
      occlusion += (occluder.z >= samplePos.z + bias ? 1.0 : 0.0) * falloff;
    }

    float visibility = 1.0 - occlusion / float(max(sampleCount, 1u));
    return float4(visibility, visibility, visibility, 1.0);
  }
}
