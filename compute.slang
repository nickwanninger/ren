struct S {
  Texture2D texture;
  SamplerState sampler;
  float x;
}

struct Node {
  int x;
  int y;
}

struct Material {
  Texture2D texture[4];
  SamplerState sampler[4];

  Node data[5];
  // Texture2D other[3];
  // S ss[2];
}

RWBuffer<float4> outputBuffer;
ParameterBlock<Material> material;
ParameterBlock<Material> material2;
Texture2D textureHeap[];
// SamplerState samplerHeap[];

[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint3 id: SV_DispatchThreadID) {
  //
  outputBuffer[id.x] = material.texture[0].Sample(material.sampler[0], float2(0, 0));
}
