import ren.core;
import ren.surface;

[[vk::push_constant]]
cbuffer root {
  TextureHandle scene;
  TextureHandle bloom;
  float2 bloomTexelSize;
  float strength;
  float exposure;
  uint bypass;
}

export struct Surface : IFullscreenSurface = BloomComposite;
struct BloomComposite : IFullscreenSurface {
  static float4 shade(FullscreenVarying input) {
    float3 color = scene.SampleLevel(input.uv, 0.0f).rgb;
    if (bypass != 0) {
      return float4(color, 1.0f);
    }
    float2 uv = clamp(input.uv, 0.5f * bloomTexelSize, 1.0f - 0.5f * bloomTexelSize);
    color += strength * bloom.SampleLevel(uv, 0.0f).rgb;
    color *= exp2(exposure);
    // ACES fitted approximation. The output attachment applies sRGB encoding.
    color = saturate((color * (2.51f * color + 0.03f)) / (color * (2.43f * color + 0.59f) + 0.14f));
    return float4(color, 1.0f);
  }
}
