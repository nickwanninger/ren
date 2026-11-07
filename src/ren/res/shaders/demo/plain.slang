// This shader draws a simple cube with a basic single-color material and phong shading.

import ren.core;

struct Material {
  float4 color;
  float metallic;
  float roughness;
  float2 padding;
};

[[vk::push_constant]]
MaterialRoot<Material> root;

struct Varying {
  float4 position : SV_Position;
  float3 normal : NORMAL;
  float2 uv : TEXCOORD1;
  nointerpolation uint drawIndex : TEXCOORD0;
};

// perform proper transformation of the vertex.
[shader("vertex")]
Varying vertexMain(Vertex input, uint drawIndex: SV_DrawIndex) {
  Varying output;

  let obj = root.getObject(drawIndex);

  output.position = obj.transformPositionToClipSpace(input.position);
  output.normal = normalize(obj.transformNormal(input.normal));
  output.uv = input.texCoord;
  output.drawIndex = drawIndex;
  return output;
}

[shader("fragment")]
GBufferOutput fragmentMain(Varying input) {
  // Renormalize: interpolating unit normals across a triangle yields vectors
  // shorter than unit length, which under-reports N.L between vertices.
  float3 N = normalize(input.normal);
  float3 L = normalize(float3(1.0, 1.0, 1.0));

  let mat = root.getMaterial(input.drawIndex);

  // float3 albedo = mat->color.rgb * texel.rgb;
  float3 albedo = mat->color.rgb;

  // The 3.0 compensates for the 1/pi below, which would otherwise darken the
  // scene ~3x purely as a side effect of normalizing the BRDF.
  const float3 lightColor = float3(3.0);
  const float3 ambientLight = float3(0.1);

  // Normalized Lambert: albedo/pi is the BRDF, max(N.L, 0) the cosine term.
  float3 diffuse = albedo * (1.0 / 3.14159265) * max(dot(N, L), 0.0) * lightColor;
  float3 ambient = albedo * ambientLight;

  GBufferOutput output;
  output.albedo = mat->color;
  output.normal = float4(N, 1.0);
  output.metallicRoughness = float4(mat->metallic, mat->roughness, 0.0, 1.0);
  return output;
}
