// Show one image on the left half and another image on the right half.

import ren.core;
import ren.surface;

[[vk::push_constant]]
cbuffer root {
  TextureHandle left;
  TextureHandle right;
}

export struct Surface : IFullscreenSurface = Split;

struct Split : IFullscreenSurface {
  static float4 shade(FullscreenVarying input) {
    if (input.uv.x < 0.5f) {
      return left.SampleLevel(input.uv, 0.0f);
    }
    return right.SampleLevel(input.uv, 0.0f);
  }
}
