import ren.core;
import ren.surface;

[[vk::push_constant]]
cbuffer root {
  TextureHandle source;
  TextureHandle detail;
  float2 sourceTexelSize;
  float2 detailTexelSize;
  uint upsample;
  uint prefilter;
  float threshold;
}

float3 sampleSource(float2 uv) {
  // The default sampler repeats. Keep the filter inside the image.
  uv = clamp(uv, 0.5f * sourceTexelSize, 1.0f - 0.5f * sourceTexelSize);
  float3 color = max(source.SampleLevel(uv, 0.0f).rgb, 0.0f);
  if (prefilter != 0) {
    float brightness = max(color.r, max(color.g, color.b));
    // A soft knee reduces abrupt changes around the threshold.
    float knee = max(threshold * 0.5f, 1e-4f);
    float soft = clamp(brightness - threshold + knee, 0.0f, 2.0f * knee);
    soft = soft * soft / (4.0f * knee);
    color *= max(brightness - threshold, soft) / max(brightness, 1e-4f);
  }
  return color;
}

export struct Surface : IFullscreenSurface = BloomFilter;
struct BloomFilter : IFullscreenSurface {
  static float4 shade(FullscreenVarying input) {
    float3 color = float3(0.0f);
    // Normalized tent filter: weights are [1, 2, 1] on each axis.
    for (int y = -1; y <= 1; ++y) {
      for (int x = -1; x <= 1; ++x) {
        float weight = (x == 0 ? 2.0f : 1.0f) * (y == 0 ? 2.0f : 1.0f);
        color += weight * sampleSource(input.uv + float2(x, y) * sourceTexelSize);
      }
    }
    color *= 1.0f / 16.0f;
    if (upsample != 0) {
      float2 uv = clamp(input.uv, 0.5f * detailTexelSize, 1.0f - 0.5f * detailTexelSize);
      // Preserve constant brightness as the pyramid levels combine.
      color = lerp(detail.SampleLevel(uv, 0.0f).rgb, color, 0.5f);
    }
    return float4(color, 1.0f);
  }
}
