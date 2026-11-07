// A pass module: the G-buffer entry points. The surface is an extern type
// that the link step resolves.

import ren.core;
import ren.surface;

extern struct Surface : ISurface;
extern struct SurfaceParams; // for the layout only
[[vk::push_constant]]
MaterialRoot<SurfaceParams> root;



struct PassVarying {
  float4 position : SV_Position;
  SurfaceVarying surface;
  nointerpolation uint drawIndex : DRAW_INDEX;
};

[shader("vertex")]
PassVarying vertexMain(Vertex input, uint drawIndex: SV_DrawIndex) {
  let obj = root.getObject(drawIndex);

  PassVarying output;
  output.surface = Surface.vertex(input, obj, (Surface.Params *)root.getMaterial(drawIndex), output.position);
  output.drawIndex = drawIndex;
  return output;
}

[shader("fragment")]
GBufferOutput fragmentMain(PassVarying input) {
  return Surface.shade(input.surface, (Surface.Params *)root.getMaterial(input.drawIndex));
}
