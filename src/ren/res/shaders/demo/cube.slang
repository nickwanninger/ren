// This shader draws a simple cube with a basic single-color material and phong shading.

import ren.core;

struct Material {
  float4 color;
};

[[vk::push_constant]]
MaterialRoot<Material> root;

struct Varying {
  float4 position : SV_Position;
  float3 normal : NORMAL;
  nointerpolation uint drawIndex : TEXCOORD0;
};

// perform proper transformation of the vertex.
[shader("vertex")]
Varying vertexMain(Vertex input, uint drawIndex: SV_DrawIndex) {
  Varying output;

  let obj = root.getObject(drawIndex);

  output.position = obj.transformPositionToClipSpace(input.position);
  output.normal = normalize(obj.transformNormal(input.normal));
  output.drawIndex = drawIndex;
  return output;
}

[shader("fragment")]
float4 fragmentMain(Varying input) : SV_Target {
  // Simple phong shading based on the normal and light direction
  float3 lightDir = normalize(float3(1.0, 1.0, 1.0));
  float diffuse = max(dot(input.normal, lightDir), 0.0);

  let mat = root.getMaterial(input.drawIndex);
  float3 ambient = 0.1 * mat->color.rgb;
  float3 finalColor = ambient + diffuse * mat->color.rgb;
  return float4(finalColor, mat->color.a);

  // return root.material.color;
}
