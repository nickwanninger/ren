// Fill a rectangle with one color, and discard the other pixels. A pass that
// updates its attachment then keeps the old contents outside the rectangle.

import ren.core;
import ren.surface;

[[vk::push_constant]]
cbuffer root {
  float4 color;
  // The rectangle in uv: (min x, min y, max x, max y). The max edge is not
  // inside the rectangle.
  float4 rect;
}

export struct Surface : IFullscreenSurface = Fill;

struct Fill : IFullscreenSurface {
  static float4 shade(FullscreenVarying input) {
    if (any(input.uv < rect.xy) || any(input.uv >= rect.zw)) {
      discard;
    }
    return color;
  }
}
