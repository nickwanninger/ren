import ren.core;

struct Varying {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

struct SquareBTransform {
  float2 center;
  float extent;
  float rotationSpeed;
};

struct SquareBConstants {
  SquareBTransform transform;
  float4 tint;
  TextureHandle pattern;
};

[[vk::push_constant]]
ConstantBuffer<SquareBConstants> root;

[shader("vertex")]
Varying vertexMain(Vertex input) {
  Varying output;
  float angle = frameGlobals.time * root.transform.rotationSpeed;
  float2x2 rotation = float2x2(cos(angle), -sin(angle), sin(angle), cos(angle));
  output.position =
      float4(mul(rotation, input.position.xy * root.transform.extent) + root.transform.center,
             input.position.z, 1.0f);
  output.uv = input.texCoord;
  return output;
}

[shader("fragment")]
float4 fragmentMain(Varying input) : SV_Target { return root.pattern.Sample(input.uv) * root.tint; }
