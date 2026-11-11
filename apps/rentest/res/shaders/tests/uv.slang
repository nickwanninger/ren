import ren.core;
import ren.surface;

export struct Surface : IFullscreenSurface = Test;

struct Test : IFullscreenSurface {
  static float4 shade(FullscreenVarying input) {
    return float4(input.uv, 0.0, 1.0);
  }
}