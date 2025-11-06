#version 450

// Pull in the engine constants (defines the UBO)
#include "shaders/engine.glsl"
#include "shaders/skylight.glsl"

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
  float occlusionStrength;
}
material;

layout(set = PBR_SET, binding = 1) uniform sampler2D baseColorTexture;
layout(set = PBR_SET, binding = 2) uniform sampler2D metallicRoughnessTexture;
layout(set = PBR_SET, binding = 3) uniform sampler2D emissiveTexture;
layout(set = PBR_SET, binding = 4) uniform sampler2D normalTexture;
// ─────────────────────────────────────────────────────────────────────────────
// PBR Constants and Utility Functions
// ─────────────────────────────────────────────────────────────────────────────

float saturate(float value) { return clamp(value, 0.0, 1.0); }

// ─────────────────────────────────────────────────────────────────────────────
// Cook-Torrance BRDF Components
// ─────────────────────────────────────────────────────────────────────────────

// Fresnel-Schlick approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
  return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// Roughness-dependent Fresnel for environment
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
  return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// GGX/Trowbridge-Reitz normal distribution function
float distributionGGX(vec3 N, vec3 H, float roughness) {
  float a = roughness * roughness;
  float a2 = a * a;
  float NdotH = max(dot(N, H), 0.0);
  float NdotH2 = NdotH * NdotH;

  float numerator = a2;
  float denominator = NdotH2 * (a2 - 1.0) + 1.0;
  denominator = PI * denominator * denominator;

  return numerator / max(denominator, 0.0001);
}

// Schlick-Beckmann geometry function (Disney remapping)
float geometrySchlickGGX(float NdotV, float roughness) {
  float r = (roughness + 1.0);
  float k = (r * r) / 8.0;

  float numerator = NdotV;
  float denominator = NdotV * (1.0 - k) + k;

  return numerator / max(denominator, 0.0001);
}

// Smith's joint visibility function
float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
  float NdotV = max(dot(N, V), 0.0);
  float NdotL = max(dot(N, L), 0.0);
  float ggx2 = geometrySchlickGGX(NdotV, roughness);
  float ggx1 = geometrySchlickGGX(NdotL, roughness);

  return ggx1 * ggx2;
}

// ─────────────────────────────────────────────────────────────────────────────
// Direct Lighting (Cook-Torrance with sun)
// ─────────────────────────────────────────────────────────────────────────────

vec3 computeDirectLighting(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, float roughness) {
  vec3 H = normalize(L + V);

  float NdotL = max(dot(N, L), 0.0);
  float NdotV = max(dot(N, V), 0.0);
  float NdotH = max(dot(N, H), 0.0);
  float LdotH = max(dot(L, H), 0.0);

  // F0 is either 0.04 (dielectric) or albedo (metal)
  vec3 F0 = mix(vec3(0.04), albedo, metallic);

  // Cook-Torrance BRDF components
  // Use VdotH instead of LdotH for Fresnel - more numerically stable at grazing angles
  float VdotH = max(dot(V, H), 0.0);
  vec3 F = fresnelSchlick(VdotH, F0);
  float D = distributionGGX(N, H, roughness);
  float G = geometrySmith(N, V, L, roughness);

  // Specular contribution
  vec3 numerator = D * G * F;
  float denominator = 4.0 * NdotV * NdotL;
  // Increased epsilon for stability at grazing angles where NdotV/NdotL approach 0
  vec3 specular = numerator / max(denominator, 0.01);

  // Energy conservation: dielectrics reflect ~4%, metals reflect their albedo
  vec3 kS = F;
  vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

  // Lambertian diffuse
  vec3 diffuse = kD * albedo / PI;

  return (diffuse + specular) * NdotL;
}

// ─────────────────────────────────────────────────────────────────────────────
// Indirect Lighting (Environment/IBL)
// ─────────────────────────────────────────────────────────────────────────────

// Sample diffuse environment lighting (Lambertian)
vec3 sampleDiffuseEnvironment(vec3 N, vec3 sunDir) {
  // return computeSkyColor(N, sunDir);
  // Sample the hemisphere around the normal for diffuse lighting
  // We approximate this by averaging samples around the normal
  const int samples = 16;
  vec3 diffuseLight = vec3(0.0);

  // Create an orthonormal basis for hemisphere sampling
  vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
  vec3 right = normalize(cross(up, N));
  up = cross(N, right);

  // Sample environment in hemisphere with regular grid
  float step = 1.0 / float(samples);
  for (int i = 0; i < samples; i++) {
    for (int j = 0; j < samples; j++) {
      float u = float(i) * step;
      float v = float(j) * step;

      // Cosine-weighted hemisphere sampling
      float theta = acos(sqrt(1.0 - u));
      float phi = 2.0 * PI * v;

      vec3 sampleDir =
          normalize(sin(theta) * cos(phi) * right + sin(theta) * sin(phi) * up + cos(theta) * N);

      diffuseLight += computeSkyColor(sampleDir, sunDir);
    }
  }

  return diffuseLight / float(samples * samples);
}

// Sample specular environment lighting with roughness-based blur
vec3 sampleSpecularEnvironment(vec3 N, vec3 V, float roughness, vec3 sunDir) {
  // return vec3(0.0);
  // return computeSkyColor(N, sunDir);
  vec3 R = reflect(-V, N);

  // Roughness-based cone for environment sampling
  const int samples = 16;
  vec3 specularLight = vec3(0.0);

  // Create orthonormal basis
  vec3 up = abs(R.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
  vec3 right = normalize(cross(up, R));
  up = cross(R, right);

  // Sample cone around reflection direction
  float maxAngle = roughness * PI / 4.0;  // Rougher materials = wider cone

  float step = 1.0 / float(samples);
  for (int i = 0; i < samples; i++) {
    for (int j = 0; j < samples; j++) {
      float u = float(i) * step;
      float v = float(j) * step;

      // Uniform cone sampling
      float theta = u * maxAngle;
      float phi = 2.0 * PI * v;

      vec3 sampleDir = normalize(cos(theta) * R + sin(theta) * (cos(phi) * right + sin(phi) * up));

      specularLight += computeSkyColor(sampleDir, sunDir);
    }
  }

  return specularLight / float(samples * samples);
}

// Indirect lighting contribution
vec3 computeIndirectLighting(vec3 N, vec3 V, vec3 albedo, float metallic, float roughness,
                             vec3 sunDir) {
  // F0 is either 0.04 (dielectric) or albedo (metal)
  vec3 F0 = mix(vec3(0.04), albedo, metallic);
  float NdotV = max(dot(N, V), 0.0);

  // Fresnel with roughness for environment
  vec3 F = fresnelSchlickRoughness(NdotV, F0, roughness);

  // Sample diffuse and specular environment
  vec3 diffuseEnv = sampleDiffuseEnvironment(N, sunDir);
  vec3 specularEnv = sampleSpecularEnvironment(N, V, roughness, sunDir);

  // Energy conservation
  vec3 kS = F;
  vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

  // Combine indirect lighting
  vec3 indirect = kD * albedo * diffuseEnv / PI;
  indirect += kS * specularEnv;

  return indirect;
}


// ─────────────────────────────────────────────────────────────────────────────
// Normal Mapping
// ─────────────────────────────────────────────────────────────────────────────

vec3 computeWorldNormal() {
  vec3 normal = texture(normalTexture, uv).xyz * 2.0 - 1.0;

  // Construct orthonormal TBN matrix
  vec3 N = normalize(worldNormal);
  vec3 T = normalize(worldTangent);
  vec3 B = normalize(worldBitangent);

  mat3 TBN = mat3(T, cross(N, T), N);

  vec3 outN = normalize(TBN * normal);

  return outN;
}



// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

void main() {
  // outColor = vec4(normalize(worldTangent), 1.0);
  // outNormal = vec4(normalize(worldBitangent), 1.0);
  // return;
  // TEMP

  // Sample and prepare textures
  vec4 baseColor = texture(baseColorTexture, uv) * material.baseColorFactor;
  if (baseColor.a < 0.01) discard;

  vec3 metallicRoughnessSample = texture(metallicRoughnessTexture, uv).rgb;
  float occlusion = metallicRoughnessSample.r;
  float metallic = saturate(metallicRoughnessSample.b * material.metallicFactor);
  float roughness = saturate(metallicRoughnessSample.g * material.roughnessFactor);
  roughness = max(roughness, 0.04);  // Clamp to avoid division by zero

  // Compute lighting vectors
  vec3 N = computeWorldNormal();
  vec3 V = normalize(engine.cameraWorldPosition.xyz - worldPos);
  vec3 L = normalize(engine.lightDirection.xyz);

  // Direct lighting from sun
  vec3 directLight = computeDirectLighting(N, V, L, baseColor.rgb, metallic, roughness);

  // Indirect lighting from environment
  vec3 indirectLight = computeIndirectLighting(N, V, baseColor.rgb, metallic, roughness, L);

  // Combine all lighting
  vec3 color = indirectLight + directLight;

  // Add emissive
  color += texture(emissiveTexture, uv).rgb * material.emissiveFactor.rgb;

  outColor = vec4(color, baseColor.a);
  outNormal = vec4(N * 0.5 + 0.5, 1.0);
}