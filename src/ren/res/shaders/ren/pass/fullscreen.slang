// A pass module: a fullscreen triangle that runs a fullscreen surface for
// each pixel. The surface is an extern type that the link step resolves.

import ren.core;
import ren.surface;

extern struct Surface : IFullscreenSurface;

struct PassVarying {
  float4 position : SV_Position;
  FullscreenVarying surface;
};

// The standard vertexless fullscreen triangle: three vertices that cover the
// whole viewport, with UVs from the vertex index, not from a buffer. The
// surface cannot change this vertex stage.
[shader("vertex")]
PassVarying vertexMain(uint vertexIndex: SV_VertexID) {
  PassVarying output;
  output.surface.uv = float2((vertexIndex << 1) & 2, vertexIndex & 2);
  output.position = float4(output.surface.uv * 2.0f - 1.0f, 0.0f, 1.0f);
  return output;
}

[shader("fragment")]
float4 fragmentMain(PassVarying input) : SV_Target {
  return Surface.shade(input.surface);
}


