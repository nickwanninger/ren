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
  // The G-buffer world-space normal, stored raw (not remapped to [0, 1]).
  TextureHandle normal;
  // The size of the SSAO target in pixels. It selects the kernel rotation of
  // each pixel.
  float2 targetSize;
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

struct SSAO : IFullscreenSurface {
  static float4 shade(FullscreenVarying input) {
    // SampleLevel everywhere: the loop below has non-uniform control flow,
    // where implicit derivatives are not defined.
    float d = depth.SampleLevel(input.uv, 0.0).r;

    // Nothing was drawn here (the sky), so there is no occlusion.
    if (d >= 1.0) {
      return float4(1.0);
    }

    float3 P = viewPositionFromDepth(input.uv, d);

    // World to view space: the rotation part of the view matrix.
    float3 worldNormal = normal.SampleLevel(input.uv, 0.0).xyz;
    float3 N = normalize(mul((float3x3)frameGlobals.viewMatrix, worldNormal));

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

      float sceneZ = viewPositionFromDepth(sampleUV, depth.SampleLevel(sampleUV, 0.0).r).z;

      // The camera looks down -Z, so a larger z is nearer. The sample is
      // occluded when the surface at its pixel is in front of it. The range
      // check ignores surfaces far outside the hemisphere.
      float rangeCheck = abs(P.z - sceneZ) < radius ? 1.0 : 0.0;
      occlusion += (sceneZ >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
    }

    float visibility = 1.0 - occlusion / float(max(sampleCount, 1u));
    return float4(visibility, visibility, visibility, 1.0);
  }
}
