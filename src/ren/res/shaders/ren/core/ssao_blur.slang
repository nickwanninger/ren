// Joint bilateral blur for the SSAO output, as a fullscreen surface. Link it
// with ren/pass/fullscreen. The depth and normal of the G-buffer guide the
// weights, so the blur does not cross geometry edges. The output can be
// larger than the SSAO input; the filter then also upsamples it.

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
  // 1 / the size of the SSAO image. The taps step in SSAO texels.
  float2 ssaoTexelSize;
}

export struct Surface : IFullscreenSurface = SSAOBlur;

static const int kRadius = 2;
// Spatial falloff, in SSAO texels.
static const float kSigmaSpatial = 1.5;
// Depth falloff, as a fraction of the center's view-space distance.
static const float kSigmaDepth = 0.1;
// Normal falloff, on 1 - dot(N, Nc)^2.
static const float kSigmaNormal = 1.0;

// View-space distance from the camera for a depth value. See
// viewPositionFromDepth in ssao.slang for the derivation.
float linearDepth(float d) {
  let P = frameGlobals.projMatrix;
  return P[2][3] / (d + P[2][2]);
}

float gaussian(float x, float sigma) { return exp(-(x * x) / (2.0 * sigma * sigma)); }

struct SSAOBlur : IFullscreenSurface {
  static float4 shade(FullscreenVarying input) {
    float centerDepth = depth.SampleLevel(input.uv, 0.0).r;

    // The sky has no normal (it is zero) and no occlusion.
    if (centerDepth >= 1.0) {
      return float4(1.0);
    }

    float centerZ = linearDepth(centerDepth);
    float3 centerNormal = normalize(normal.SampleLevel(input.uv, 0.0).xyz);

    float result = 0.0;
    float weightSum = 0.0;
    for (int y = -kRadius; y <= kRadius; ++y) {
      for (int x = -kRadius; x <= kRadius; ++x) {
        float2 offset = float2(float(x), float(y));
        // Clamp: the graph sampler repeats, and a tap at the edge must not
        // read the other side of the screen.
        float2 uv = clamp(input.uv + offset * ssaoTexelSize, 0.0, 1.0);

        float sampleDepth = depth.SampleLevel(uv, 0.0).r;
        if (sampleDepth >= 1.0) {
          continue;
        }

        float3 sampleNormal = normalize(normal.SampleLevel(uv, 0.0).xyz);
        float normalSim = max(0.0, dot(sampleNormal, centerNormal));

        float weight = gaussian(length(offset), kSigmaSpatial) *
                       gaussian((linearDepth(sampleDepth) - centerZ) / centerZ, kSigmaDepth) *
                       exp(-(1.0 - normalSim * normalSim) / (2.0 * kSigmaNormal * kSigmaNormal));

        result += ssao.SampleLevel(uv, 0.0).r * weight;
        weightSum += weight;
      }
    }

    // The center tap is not sky and has weight 1, so weightSum >= 1.
    float visibility = result / weightSum;
    return float4(visibility, visibility, visibility, 1.0);
  }
}
