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

  // S foo[4];
  float x;
  // Texture2D A, B, C;
}

struct Output {
  RWBuffer<float4> outputBuffer;
};

ParameterBlock<Output> output;

ParameterBlock<Material> material;

[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint3 id: SV_DispatchThreadID) {

  output.outputBuffer[id.x] = material.texture.Sample(material.sampler, float2(0, 0)) * material.x;

  // float4 color = material.texture[id.x].Sample(material.sampler[id.y], float2(0, 0)) * material.foo[0].x;
  // output.outputBuffer[id.x] = color;
}

// struct Material {
//   Texture2D texture[2];
//   SamplerState samplers[3];
// }

// ParameterBlock<Material> globalMat;
// RWBuffer<float4> output;

// [shader("compute")]
// [numthreads(16, 16, 1)]
// void computeMain(uint3 id: SV_DispatchThreadID) {
//   float4 color = float4(0);
//   color += globalMat.texture[id.y].Sample(globalMat.samplers[id.z], float2(0, 0));
//   output[id.x] = color;
// }
