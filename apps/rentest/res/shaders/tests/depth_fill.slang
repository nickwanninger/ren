// Fill a rectangle with one color at one depth. This is a whole program, not
// a surface: ren/pass/fullscreen draws at depth 0, and its vertex stage cannot
// change. The vertex stage here puts the triangle at the depth from the root.

import ren.core;

[[vk::push_constant]]
cbuffer root {
  float4 color;
  // The rectangle in uv: (min x, min y, max x, max y). The max edge is not
  // inside the rectangle.
  float4 rect;
  // The depth of the triangle, in [0, 1].
  float depth;
}

struct Varying {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

[shader("vertex")]
Varying vertexMain(uint vertexIndex: SV_VertexID) {
  Varying output;
  output.uv = float2((vertexIndex << 1) & 2, vertexIndex & 2);
  output.position = float4(output.uv * 2.0f - 1.0f, depth, 1.0f);
  return output;
}

[shader("fragment")]
float4 fragmentMain(Varying input) : SV_Target {
  if (any(input.uv < rect.xy) || any(input.uv >= rect.zw)) {
    discard;
  }
  return color;
}
