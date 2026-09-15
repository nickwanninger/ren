import ren.core;

struct Varying {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

struct SquareAConstants {
  float2 offset;
  float scale;
  float pulseAmount;
  TextureHandle image;
};

[[vk::push_constant]]
ConstantBuffer<SquareAConstants> root;

[shader("vertex")]
Varying vertexMain(Vertex input) {
  Varying output;
  float pulse = 1.0f + sin(frameGlobals.time) * root.pulseAmount;
  output.position =
      float4(input.position.xy * root.scale * pulse + root.offset, input.position.z, 1.0f);
  output.uv = input.texCoord;
  return output;
}

[shader("fragment")]
float4 fragmentMain(Varying input) : SV_Target { return root.image.Sample(input.uv); }
