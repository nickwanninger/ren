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
  // RGB noise in [0, 1] that rotates the kernel per pixel. It must repeat.
  TextureHandle noise;
  // UV scale that maps one noise texel to one output pixel.
  float2 noiseScale;
  // The view-space radius of the sample hemisphere.
  float radius;
  // Depth bias against self-occlusion on flat surfaces.
  float bias;
  // The number of kernel samples.
  uint sampleCount;
}

export struct Surface : IFullscreenSurface = SSAO;

// The projection is glm::perspective (right-handed, depth in [0, 1]) with Y
// flipped for Vulkan. Matrix indices select rows (see viewport_clear.slang):
//   clip = (P00 x, P11 y, P22 z + P23, -z)
// so depth = (P22 z + P23) / -z, and z = -P23 / (depth + P22).
float3 viewPositionFromDepth(float2 uv, float depthValue) {
  let P = frameGlobals.projMatrix;
  float z = -P[2][3] / (depthValue + P[2][2]);
  float2 ndc = uv * 2.0 - 1.0;
  return float3(ndc.x * -z / P[0][0], ndc.y * -z / P[1][1], z);
}

// Project a view-space position to a UV in the same convention as the
// fullscreen pass (uv = ndc * 0.5 + 0.5).
float2 uvFromViewPosition(float3 p) {
  float4 clip = mul(frameGlobals.projMatrix, float4(p, 1.0));
  return clip.xy / clip.w * 0.5 + 0.5;
}

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

    // A tangent frame around N, rotated by the noise (Gram-Schmidt).
    float3 randomVec = normalize(noise.SampleLevel(input.uv * noiseScale, 0.0).xyz * 2.0 - 1.0);
    float3 T = normalize(randomVec - N * dot(randomVec, N));
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
