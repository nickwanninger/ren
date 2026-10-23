// A surface module: one ISurface implementation and no entry points.

import ren.core;
import ren.surface;


struct Red : ISurface {

  struct Params {
    float4 color;
  }

  // No vertex(): Red uses the standard transform from ISurface.

  static GBufferOutput shade(SurfaceVarying v, Params *params) {
    GBufferOutput o;
    o.albedo = params.color;
    // The G-buffer stores normals in [0, 1].
    o.normal = float4(normalize(v.worldNormal) * 0.5 + 0.5, 1.0);
    o.metallicRoughness = float4(0.0, 0.5, 0.0, 1.0);
    return o;
  }
}

// Satisfies `extern struct Surface : ISurface` in the pass module.
export struct Surface : ISurface = Red;
