struct S {
  Texture2D texture;
  SamplerState sampler;
  float x;
}

// struct Node {
//   int x;
//   int y;
// }

struct Material {
  Texture2D texture;
  SamplerState sampler;
  float x;
}

struct Output {
  RWBuffer<float4> outputBuffer;
};

[[vk::push_constant]]
cbuffer pc {
  float4 rootColor;
  float scale;
}


ParameterBlock<Output> output;
ParameterBlock<Material> material;


[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint3 id: SV_DispatchThreadID, uniform ParameterBlock<Material> mat) {

  float4 color = rootColor;

  color += material.texture.Sample(material.sampler, float2(0, 0)) * material.x;
  color += mat.texture.Sample(mat.sampler, float2(0, 0)) * material.x;

  output.outputBuffer[id.x] = color;
}