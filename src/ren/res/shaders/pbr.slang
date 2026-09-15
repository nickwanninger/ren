import ren.core;


struct PBRMaterial {
  TextureHandle baseColorTexture;
  TextureHandle metallicRoughnessTexture;
  TextureHandle emissiveTexture;
  TextureHandle normalTexture;

  float4 baseColorFactor;
  float4 emissiveFactor;

  float metallicFactor;
  float roughnessFactor;
  float occlusionStrength;
}



[vk::push_constant]
cbuffer pc {
  float4x4 model;
  float4x4 view;
  float4x4 proj;
  float4x4 normalMatrix;
}


struct Varying {
  float4 position : SV_Position;
  float3 worldNormal : NORMAL;
  float2 uv : TEXCOORD0;
  float3 worldPos : TEXCOORD1;
  float3 worldTangent : TEXCOORD2;
  float3 worldBitangent : TEXCOORD3;
}

[shader("vertex")]
Varying vertexMain(Vertex input) {
  Varying output;


  output.position = transformToClipSpace(input.position, model, view, proj);


  float3x3 normalMat = float3x3(normalMatrix);

  float3 T = input.tangent;
  float3 N = input.normal;
  float3 B = input.bitangent;
  // re-orthogonalize T with respect to N
  T = normalize(T - dot(T, N) * N);

  float sign = (dot(cross(N, T), B) < 0.0f) ? -1.0f : 1.0f;
  B = cross(N, T) * sign;

  output.worldNormal = normalize(mul(normalMat, N));
  output.worldTangent = normalize(mul(normalMat, T));
  output.worldBitangent = normalize(mul(normalMat, B));
  output.worldPos = mul(model, float4(input.position, 1.0f)).xyz;

  return output;
}

[shader("fragment")]
GBufferOutput fragmentMain(Varying input) : SV_Target {
  GBufferOutput output;

  output.albedo = float4(1.0, 1.0, 1.0, 1.0);
  output.metallicRoughness = float4(0.0, 1.0, 0.0, 1.0);
  output.normal = float4(normalize(input.worldNormal) * 0.5 + 0.5, 1.0);

  return output;
}
