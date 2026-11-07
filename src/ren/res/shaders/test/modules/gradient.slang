// A fullscreen surface module: one IFullscreenSurface implementation and no
// entry points.

import ren.core;
import ren.surface;

[[vk::push_constant]]
cbuffer root {
  float4 top;
  float4 bottom;
}

export struct Surface : IFullscreenSurface = Gradient;

struct Gradient : IFullscreenSurface {
  static float4 shade(FullscreenVarying v) {
    // Simple lerp using the push constants.
    return lerp(top, bottom, v.uv.y);
  }
}

