// Fullscreen PBR lighting, sky, and world-space grid.

import ren.core;
import ren.surface;

[[vk::push_constant]]
cbuffer root {
  float ssaoContrast;
  TextureHandle skyboxHdri;
  // The G-buffer albedo (linear), world-space normal (raw), depth, and the
  // blurred SSAO visibility.
  TextureHandle albedo;
  TextureHandle normal;
  // R is metallic, G is roughness, and B is material occlusion.
  TextureHandle metallicRoughness;
  TextureHandle depth;
  TextureHandle ssao;
}

// The direction points from the surface toward the sun.
static const float3 kSunDirection = float3(0.35f, 0.85f, 0.4f);
static const float3 kSunColor = float3(1.0f, 0.95f, 0.85f) * 3.0f;
static const float3 kAmbientColor = float3(0.55f, 0.6f, 0.7f) * 0.35f;
static const float kPi = 3.14159265f;


float3 fresnelSchlick(float cosine, float3 f0) {
  return f0 + (1.0f - f0) * pow(1.0f - saturate(cosine), 5.0f);
}


float3 directLighting(float3 N, float3 V, float3 base, float metallic, float roughness) {
  float3 L = normalize(kSunDirection);
  float nDotL = saturate(dot(N, L));
  float nDotV = saturate(dot(N, V));
  if (nDotL <= 0.0f || nDotV <= 0.0f) {
    return float3(0.0f);
  }

  float3 H = normalize(V + L);
  float nDotH = saturate(dot(N, H));
  float alpha = roughness * roughness;
  float alphaSquared = alpha * alpha;
  float denominator = (1.0f - nDotH * nDotH) + nDotH * nDotH * alphaSquared;
  float distribution = alphaSquared / (kPi * denominator * denominator);

  // Correlated Smith visibility includes the Cook-Torrance denominator.
  float smithV = nDotL * sqrt(nDotV * nDotV * (1.0f - alphaSquared) + alphaSquared);
  float smithL = nDotV * sqrt(nDotL * nDotL * (1.0f - alphaSquared) + alphaSquared);
  float visibility = 0.5f / max(smithV + smithL, 0.000001f);
  float3 f0 = lerp(float3(0.04f), base, metallic);
  float3 fresnel = fresnelSchlick(dot(V, H), f0);
  float3 specular = distribution * visibility * fresnel;
  float3 diffuse = (1.0f - fresnel) * (1.0f - metallic) * base / kPi;
  return (diffuse + specular) * kSunColor * nDotL;
}


float3 ambientLighting(float3 N, float3 V, float3 base, float metallic, float roughness) {
  float nDotV = saturate(dot(N, V));
  float3 f0 = lerp(float3(0.04f), base, metallic);

  // Approximate the integrated GGX BRDF for a constant environment.
  // The sky image is not a prefiltered environment map.
  float4 r = roughness * float4(-1.0f, -0.0275f, -0.572f, 0.022f)
      + float4(1.0f, 0.0425f, 1.04f, -0.04f);
  float a004 = min(r.x * r.x, exp2(-9.28f * nDotV)) * r.x + r.y;
  float2 brdf = float2(-1.04f, 1.04f) * a004 + r.zw;
  float3 specular = saturate(f0 * brdf.x + brdf.y);
  float3 diffuse = (1.0f - specular) * (1.0f - metallic) * base;
  return (diffuse + specular) * kAmbientColor;
}

export static const float kAlphaCutoff = 0.4;
export static const bool kWriteDepth = false;
export static const bool kReadDepth = true;

export struct Surface : IFullscreenSurface = Viewport;

struct Viewport : IFullscreenSurface {
  static float4 shade(FullscreenVarying input) {
    // The sky and grid run for every pixel, also under geometry. They use
    // implicit derivatives (fwidth, Sample), which are undefined when a
    // neighbor in the 2x2 pixel quad has returned. An early return for
    // geometry gave random grid colors along silhouettes.
    float2 ndc = input.uv * 2.0f - 1.0f;

    float tanX = ndc.x / frameGlobals.projMatrix[0][0];
    float tanY = ndc.y / frameGlobals.projMatrix[1][1];

    // In view space the camera looks down -Z, so the ray is (x, y, -1).
    float3 viewRay = float3(tanX, tanY, -1.0f);

    // Shaders here are compiled SLANG_MATRIX_LAYOUT_COLUMN_MAJOR, so matrices
    // arrive with GLM's own layout and mul(M, v) behaves as it does in GLSL.
    // A view matrix's rotation is orthonormal, so its transpose is its inverse:
    // that is what carries a view-space direction back into world space.
    float3x3 viewRotation = (float3x3)frameGlobals.viewMatrix;
    float3 rayDir = normalize(mul(transpose(viewRotation), viewRay));

    // The view translation is column 3 (matrix indices here select rows).
    float3 viewTranslation =
        float3(frameGlobals.viewMatrix[0][3], frameGlobals.viewMatrix[1][3], frameGlobals.viewMatrix[2][3]);
    float3 cameraPos = -mul(transpose(viewRotation), viewTranslation);

    // Map world direction to the equirectangular texture. The image center
    // faces -Z, its top faces +Y, and its bottom faces -Y.
    const float invPi = 0.318309886f;
    float2 skyUV =
        float2(0.5f + atan2(rayDir.x, -rayDir.z) * (0.5f * invPi), acos(clamp(rayDir.y, -1.0f, 1.0f)) * invPi);

    float3 color = skyboxHdri.Sample(skyUV).rgb;

    // Unit grid on the y = 0 plane, with thicker lines every ten cells.
    // Fade it out by 60 units and suppress lines smaller than one pixel.
    // fwidth runs for all pixels, outside the branch, for the same reason as
    // above: at the horizon, t changes sign between neighbors.
    // float safeRayY = abs(rayDir.y) > 0.001f ? rayDir.y : 0.001f;
    // float t = -cameraPos.y / safeRayY;
    // float2 hit = (cameraPos + rayDir * t).xz;
    // float2 width = max(fwidth(hit), float2(0.0001f));
    // if (abs(rayDir.y) > 0.001f && t > 0.0f) {
    //   float2 minorDistance = abs(frac(hit + 0.5f) - 0.5f);
    //   float2 majorDistance = abs(frac(hit / 10.0f + 0.5f) - 0.5f) * 10.0f;
    //   float2 minorLines = 1.0f - smoothstep(float2(0.0f), width, minorDistance);
    //   float2 majorLines = 1.0f - smoothstep(float2(0.0f), 2.0f * width, majorDistance);
    //   float pixelFootprint = max(width.x, width.y);
    //   float minor = max(minorLines.x, minorLines.y) * saturate(1.0f - pixelFootprint);
    //   float major = max(majorLines.x, majorLines.y) * saturate(1.0f - pixelFootprint / 10.0f);
    //   float fade = saturate(1.0f - t / 60.0f);
    //   color = lerp(color, float3(0.85f), max(minor, major) * fade * 0.6f);

    //   // X is red and Z is blue. Both axes use the major line width.
    //   float axisVisibility = saturate(1.0f - pixelFootprint / 10.0f);
    //   float xAxis = (1.0f - smoothstep(0.0f, 2.0f * width.y, abs(hit.y))) * axisVisibility;
    //   float zAxis = (1.0f - smoothstep(0.0f, 2.0f * width.x, abs(hit.x))) * axisVisibility;
    //   float3 axisColor =
    //       (float3(1.0f, 0.1f, 0.1f) * xAxis + float3(0.1f, 0.35f, 1.0f) * zAxis) / max(xAxis + zAxis, 0.0001f);
    //   color = lerp(color, axisColor, max(xAxis, zAxis) * fade * 1.6f);
    // }

    // Occlusion affects ambient lighting, because it describes sky visibility.
    float d = depth.SampleLevel(input.uv, 0.0f).r;
    if (d < 1.0f) {
      float3 base = albedo.SampleLevel(input.uv, 0.0f).rgb;
      float3 N = normalize(normal.SampleLevel(input.uv, 0.0f).xyz);
      float3 viewPosition = viewPositionFromDepth(input.uv, d);
      float3 V = normalize(mul(transpose(viewRotation), -viewPosition));
      float3 material = metallicRoughness.SampleLevel(input.uv, 0.0f).rgb;
      float metallic = saturate(material.r);
      float roughness = clamp(material.g, 0.05f, 1.0f);
      float visibility = ssao.SampleLevel(input.uv, 0.0f).r;
      visibility = pow(saturate(visibility), ssaoContrast) * saturate(material.b);

      color = directLighting(N, V, base, metallic, roughness)
          + ambientLighting(N, V, base, metallic, roughness) * visibility;
    }

    return float4(color, 1.0f);
  }
}
