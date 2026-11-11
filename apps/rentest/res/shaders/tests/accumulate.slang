// Add a step to the history of the image. Without history (the first frame),
// start from zero.

import ren.core;
import ren.surface;

[[vk::push_constant]]
cbuffer root {
  TextureHandle history;
  float4 step;
}

export struct Surface : IFullscreenSurface = Accumulate;

struct Accumulate : IFullscreenSurface {
  static float4 shade(FullscreenVarying input) {
    float4 previous = history.isValid() ? history.SampleLevel(input.uv, 0.0f) : float4(0.0f);
    return previous + step;
  }
}
