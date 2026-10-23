// This shader draws a simple cube with a basic single-color material and phong shading.

import ren.core;

struct Material {

  float A;
  float2 B;
  float3 C;
  float4 D;
  float3x3 E;
  float4x4 F;

  uint G;
  uint2 H;

  float4 color;
  TextureHandle tex;
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
float4 fragmentMain(Varying input) : SV_Target {
  // Renormalize: interpolating unit normals across a triangle yields vectors
  // shorter than unit length, which under-reports N.L between vertices.
  float3 N = normalize(input.normal);
  float3 L = normalize(float3(1.0, 1.0, 1.0));

  let mat = root.getMaterial(input.drawIndex);

  // Albedo is a property of the surface, so it is factored out once and shared
  // by every lighting term. Applying the texture to the diffuse term alone made
  // unlit faces fade to an untextured flat color.
  //
  // NOTE: the texture is R8G8B8A8_SRGB, so the sample is hardware-decoded to
  // linear, but mat->color is still uploaded gamma-encoded - the two are being
  // multiplied in mismatched spaces, so the tint reads brighter than authored.
  float4 texel = mat->tex.Sample(input.uv);
  // float3 albedo = mat->color.rgb * texel.rgb;
  float3 albedo = texel.rgb;

  // The 3.0 compensates for the 1/pi below, which would otherwise darken the
  // scene ~3x purely as a side effect of normalizing the BRDF.
  const float3 lightColor = float3(3.0);
  const float3 ambientLight = float3(0.1);

  // Normalized Lambert: albedo/pi is the BRDF, max(N.L, 0) the cosine term.
  float3 diffuse = albedo * (1.0 / 3.14159265) * max(dot(N, L), 0.0) * lightColor;
  float3 ambient = albedo * ambientLight;

  return float4(ambient + diffuse, mat->color.a * texel.a);
}
