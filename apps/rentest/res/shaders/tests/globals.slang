// Write the frame globals of the view to the color, so that a test can check
// them: (time, deltaTime, renderSize.x / 1024, renderSize.y / 1024).

import ren.core;
import ren.surface;

export struct Surface : IFullscreenSurface = Globals;

struct Globals : IFullscreenSurface {
  static float4 shade(FullscreenVarying input) {
    return float4(frameGlobals.time, frameGlobals.deltaTime, frameGlobals.renderSize / 1024.0f);
  }
}
