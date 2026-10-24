// Show the image of a test in the window: as large as it fits, with its aspect
// ratio, and centered, on black.
//
// The swapchain is sRGB, so the hardware encodes each output value. An image
// in an sRGB format decodes when it is sampled, so its stored bytes come back
// out unchanged. A UNORM image does not decode, so its values must decode
// here. Then the window shows the stored bytes as sRGB colors, the same as an
// image viewer shows the PNG of the test.

import ren.core;
import ren.surface;

[[vk::push_constant]]
cbuffer root {
  TextureHandle image;
  // Where the image is in the window, in window uv: (min x, min y, max x, max y).
  float4 rect;
  // Not zero if the values of the image must decode from sRGB.
  uint decodeSrgb;
}

export struct Surface : IFullscreenSurface = Display;

float srgbToLinear(float value) {
  return value <= 0.04045f ? value / 12.92f : pow((value + 0.055f) / 1.055f, 2.4f);
}

struct Display : IFullscreenSurface {
  static float4 shade(FullscreenVarying input) {
    if (any(input.uv < rect.xy) || any(input.uv >= rect.zw)) {
      return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    float2 uv = (input.uv - rect.xy) / (rect.zw - rect.xy);
    float4 color = image.SampleLevel(uv, 0.0f);
    if (decodeSrgb != 0) {
      color.rgb = float3(srgbToLinear(color.r), srgbToLinear(color.g), srgbToLinear(color.b));
    }
    return float4(color.rgb, 1.0f);
  }
}
