#version 450


// Pull in the engine constants (defines the UBO)
#include "shaders/engine.glsl"

layout(location = 0) in vec3 worldNormal;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec3 worldPos;
layout(location = 3) in vec3 worldTangent;
layout(location = 4) in vec3 worldBitangent;


// We must emit both albedo and a normal value for postprocessing.
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outNormal;




layout(set = PBR_SET, binding = 0, std140) uniform MaterialUBO {
  vec4 baseColorFactor;
  vec4 emissiveFactor;
  float metallicFactor;
  float roughnessFactor;
}
material;

layout(set = PBR_SET, binding = 1) uniform sampler2D baseColorTexture;
layout(set = PBR_SET, binding = 2) uniform sampler2D metallicRoughnessTexture;
layout(set = PBR_SET, binding = 3) uniform sampler2D normalTexture;



vec3 computeWorldNormal() {
  // Sample the normal map in tangent space
  vec3 normalMapSample = texture(normalTexture, uv).xyz * 2.0 - 1.0;
  // normalMapSample.xy = normalMapSample.xy * 2.0 - 1.0;
  // Grab the TBN matrix
  vec3 N = normalize(worldNormal);
  // return N;
  vec3 T = normalize(worldTangent);
  // return T;
  // vec3 B = normalize(worldBitangent);
  vec3 B = cross(T, N);
  // return B;
  mat3 TBN = mat3(T, B, N);
  // Transform the normal map sample to world space to produce the final normal
  return normalize(TBN * normalMapSample);
}


float saturate(float value) { return clamp(value, 0.0, 1.0); }

vec3 fresnelSchlick(float cosTheta, vec3 F0) { return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0); }

float distributionGGX(vec3 N, vec3 H, float roughness) {
  float a = roughness * roughness;
  float a2 = a * a;
  float NdotH = max(dot(N, H), 0.0);
  float NdotH2 = NdotH * NdotH;

  float num = a2;
  float denom = (NdotH2 * (a2 - 1.0) + 1.0);
  denom = 3.14159265 * denom * denom;

  return num / denom;
}

float geometrySchlickGGX(float NdotV, float roughness) {
  float r = (roughness + 1.0);
  float k = (r * r) / 8.0;

  float num = NdotV;
  float denom = NdotV * (1.0 - k) + k;

  return num / denom;
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
  float NdotV = max(dot(N, V), 0.0);
  float NdotL = max(dot(N, L), 0.0);
  float ggx2 = geometrySchlickGGX(NdotV, roughness);
  float ggx1 = geometrySchlickGGX(NdotL, roughness);

  return ggx1 * ggx2;
}

// PBR shader using Cook-Torrance BRDF

vec3 computeSkyboxColor(vec3 direction) {
  // Simple gradient skybox based on the Y component of the direction
  vec3 horizonColor = vec3(0.5, 0.7, 1.0);  // Light blue
  vec3 zenithColor = vec3(0.1, 0.2, 0.5);   // Dark blue

  float t = saturate(direction.y * 0.5 + 0.5);  // Map Y to [0, 1]
  return mix(zenithColor, horizonColor, t);
}

void main() {
  // Sample textures
  vec4 baseColor = texture(baseColorTexture, uv) * material.baseColorFactor;
  if (baseColor.a < 0.01) discard;

  vec3 metallicRoughnessSample = texture(metallicRoughnessTexture, uv).rgb;
  float metallic = clamp(metallicRoughnessSample.b * material.metallicFactor, 0.0, 1.0);
  float roughness = clamp(metallicRoughnessSample.g * material.roughnessFactor, 0.04, 1.0);

  // Lighting vectors
  vec3 N = computeWorldNormal();
  vec3 L = normalize(engine.lightDirection.xyz);
  vec3 V = normalize(engine.cameraWorldPosition.xyz - worldPos);
  vec3 H = normalize(L + V);

  float NdotL = max(dot(N, L), 0.0);
  float NdotV = max(dot(N, V), 0.0);
  float NdotH = max(dot(N, H), 0.0);
  float VdotH = max(dot(V, H), 0.0);

  // Cook-Torrance BRDF
  vec3 F0 = mix(vec3(0.04), baseColor.rgb, metallic);
  vec3 F = fresnelSchlick(VdotH, F0);
  float D = distributionGGX(N, H, roughness);
  float G = geometrySmith(N, V, L, roughness);

  // Specular BRDF
  vec3 numerator = D * G * F;
  float denominator = 4.0 * NdotV * NdotL + 0.001;
  vec3 specular = numerator / denominator;

  // Energy conservation
  vec3 kS = F;
  vec3 kD = vec3(1.0) - kS;
  kD *= 1.0 - metallic;

  // Lambertian diffuse
  vec3 diffuse = kD * baseColor.rgb / 3.14159265;

  // Simple ambient lighting
  vec3 ambient = vec3(0.1, 0.1, 0.1) * baseColor.rgb;


  // Final color with skybox contribution
  vec3 color = ambient + (diffuse + specular) * NdotL;
  // color = material.emissiveFactor.rgb;
  color += material.emissiveFactor.rgb;

  outColor = vec4(color, baseColor.a);
  outNormal = vec4(N * 0.5 + 0.5, 1.0);
}