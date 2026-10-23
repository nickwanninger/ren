// Fill a rectangle in two color attachments at the same time: color0 into the
// first attachment, and color1 into the second. This is a whole program,
// because a fullscreen surface returns one color.

import ren.core;

[[vk::push_constant]]
cbuffer root {
  float4 color0;
  float4 color1;
  // The rectangle in uv: (min x, min y, max x, max y). The max edge is not
  // inside the rectangle.
  float4 rect;
}

struct Varying {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

struct Targets {
  float4 first : SV_Target0;
  float4 second : SV_Target1;
};

[shader("vertex")]
Varying vertexMain(uint vertexIndex: SV_VertexID) {
  Varying output;
  output.uv = float2((vertexIndex << 1) & 2, vertexIndex & 2);
  output.position = float4(output.uv * 2.0f - 1.0f, 0.0f, 1.0f);
  return output;
}

[shader("fragment")]
Targets fragmentMain(Varying input) {
  if (any(input.uv < rect.xy) || any(input.uv >= rect.zw)) {
    discard;
  }
  Targets output;
  output.first = color0;
  output.second = color1;
  return output;
}
