// Joint bilateral blur and upsample for the SSAO output, as a fullscreen
// surface. Link it with ren/pass/fullscreen. The output can be larger than
// the SSAO input.
//
// The SSAO rotates its kernel in a 4x4 pixel pattern. This blur reads the
// 4x4 SSAO texels around the pixel, so it reads each cell of the pattern one
// time and averages all 16 rotations. The taps are on texel centers, and each
// tap reads its depth and normal at the same point: no value is blended
// across an edge before the weights apply.

import ren.core;
import ren.surface;

[[vk::push_constant]]
cbuffer root {
  // The SSAO visibility in R.
  TextureHandle ssao;
  // The G-buffer depth (D32, [0, 1], 1 is the far plane).
  TextureHandle depth;
  // The G-buffer world-space normal, stored raw.
  TextureHandle normal;
  // 1 / the size of the SSAO image.
  float2 ssaoTexelSize;
  // The size of the depth image. It can be larger than the SSAO image.
  float2 depthSize;
}

export struct Surface : IFullscreenSurface = SSAOBlur;

// Plane falloff: the distance of a tap from the center's tangent plane, as a
// fraction of the center's view distance. A tap on the same surface is near
// the plane, also on a surface at a grazing angle.
static const float kSigmaPlane = 0.02;
// Normal falloff, on 1 - dot(N, Nc)^2. It is weak on purpose: a strong normal
// weight rejects neighbors over normal-map detail, and the pattern noise then
// stays.
static const float kSigmaNormal = 1.0;

float gaussian(float x, float sigma) { return exp(-(x * x) / (2.0 * sigma * sigma)); }

// The UV of the depth texel that an SSAO texel was computed from: the
// top-left texel of its block (see depthTexelUV in ssao.slang).
float2 guideUV(float2 ssaoTexel) {
  float2 texel = floor(ssaoTexel * (depthSize * ssaoTexelSize));
  return (texel + 0.5) / depthSize;
}

struct SSAOBlur : IFullscreenSurface {
  static float4 shade(FullscreenVarying input) {
    float centerDepth = depth.SampleLevel(input.uv, 0.0).r;

    // The sky has no normal (it is zero) and no occlusion.
    if (centerDepth >= 1.0) {
      return float4(1.0);
    }

    float3 centerPos = viewPositionFromDepth(input.uv, centerDepth);
    float3 centerNormal = normalize(normal.SampleLevel(input.uv, 0.0).xyz);
    float3 centerViewNormal = mul((float3x3)frameGlobals.viewMatrix, centerNormal);

    // The first of the 4x4 SSAO texels around the pixel. An SSAO texel t
    // stands for the depth texel at the top-left of its block, which is at
    // t + 0.25 in SSAO texels when the SSAO has half the size. The pixel is in
    // the middle of the four positions.
    float2 ssaoSize = 1.0 / ssaoTexelSize;
    float2 offset = 0.5 * (ssaoSize / depthSize);
    float2 base = floor(input.uv * ssaoSize - 1.0 - offset);

    float result = 0.0;
    float weightSum = 0.0;
    // For the fallback: the tap nearest to the center's plane.
    float bestPlaneDistance = 1e30;
    float bestValue = 1.0;
    for (int y = 0; y < 4; ++y) {
      for (int x = 0; x < 4; ++x) {
        // A texel, clamped into the image: the samplers repeat.
        float2 texel = clamp(base + float2(float(x), float(y)), float2(0.0), ssaoSize - 1.0);
        float2 uv = guideUV(texel);
        float value = ssao.SampleLevel((texel + 0.5) * ssaoTexelSize, 0.0).r;

        float sampleDepth = depth.SampleLevel(uv, 0.0).r;
        if (sampleDepth >= 1.0) {
          continue;
        }

        float3 samplePos = viewPositionFromDepth(uv, sampleDepth);
        float planeDistance = abs(dot(centerViewNormal, samplePos - centerPos)) / -centerPos.z;

        float3 sampleNormal = normalize(normal.SampleLevel(uv, 0.0).xyz);
        float normalSim = max(0.0, dot(sampleNormal, centerNormal));

        float weight = gaussian(planeDistance, kSigmaPlane) *
                       exp(-(1.0 - normalSim * normalSim) / (2.0 * kSigmaNormal * kSigmaNormal));

        result += value * weight;
        weightSum += weight;
        if (planeDistance < bestPlaneDistance) {
          bestPlaneDistance = planeDistance;
          bestValue = value;
        }
      }
    }

    // A pixel on a thin object can have no tap on its own surface (all sky,
    // or all far from its plane). It then takes the tap nearest to its plane,
    // not the nearest texel, which can be on the other surface.
    float visibility = weightSum > 1e-4 ? result / weightSum : bestValue;
    return float4(visibility, visibility, visibility, 1.0);
  }
}
