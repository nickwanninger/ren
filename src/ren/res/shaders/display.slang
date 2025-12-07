struct VertexParams {
  uint vertexID : SV_VertexID;
};


struct FragParams {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};


struct DisplayParams {
  Sampler2D<float4> albedo;
};

ParameterBlock<DisplayParams> params;

[shader("vertex")]
FragParams vert(VertexParams input) {
  FragParams output;

  output.uv = float2((input.vertexID << 1) & 2, input.vertexID & 2);
  output.position = float4(output.uv * 2.0f + -1.0f, 0.0f, 1.0f);

  return output;
}


[shader("fragment")]
float4 frag(FragParams input) : SV_Target {
  return params.albedo.Sample(input.uv);
}