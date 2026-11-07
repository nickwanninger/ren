// Fullscreen sky and world-space grid.

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
  TextureHandle depth;
  TextureHandle ssao;
}

// TEST lighting: one sun at a fixed direction plus a constant ambient term.
// The direction points from the surface toward the sun: high in the sky,
// from the side. There is no tone mapping, so the sum stays near 1.
static const float3 kSunDirection = float3(0.35f, 0.85f, 0.4f);
static const float3 kSunColor = float3(1.0f, 0.95f, 0.85f) * 0.9f;
static const float3 kAmbientColor = float3(0.55f, 0.6f, 0.7f) * 0.35f;

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
    float safeRayY = abs(rayDir.y) > 0.001f ? rayDir.y : 0.001f;
    float t = -cameraPos.y / safeRayY;
    float2 hit = (cameraPos + rayDir * t).xz;
    float2 width = max(fwidth(hit), float2(0.0001f));
    if (abs(rayDir.y) > 0.001f && t > 0.0f) {
      float2 minorDistance = abs(frac(hit + 0.5f) - 0.5f);
      float2 majorDistance = abs(frac(hit / 10.0f + 0.5f) - 0.5f) * 10.0f;
      float2 minorLines = 1.0f - smoothstep(float2(0.0f), width, minorDistance);
      float2 majorLines = 1.0f - smoothstep(float2(0.0f), 2.0f * width, majorDistance);
      float pixelFootprint = max(width.x, width.y);
      float minor = max(minorLines.x, minorLines.y) * saturate(1.0f - pixelFootprint);
      float major = max(majorLines.x, majorLines.y) * saturate(1.0f - pixelFootprint / 10.0f);
      float fade = saturate(1.0f - t / 60.0f);
      color = lerp(color, float3(0.85f), max(minor, major) * fade * 0.6f);

      // X is red and Z is blue. Both axes use the major line width.
      float axisVisibility = saturate(1.0f - pixelFootprint / 10.0f);
      float xAxis = (1.0f - smoothstep(0.0f, 2.0f * width.y, abs(hit.y))) * axisVisibility;
      float zAxis = (1.0f - smoothstep(0.0f, 2.0f * width.x, abs(hit.x))) * axisVisibility;
      float3 axisColor =
          (float3(1.0f, 0.1f, 0.1f) * xAxis + float3(0.1f, 0.35f, 1.0f) * zAxis) / max(xAxis + zAxis, 0.0001f);
      color = lerp(color, axisColor, max(xAxis, zAxis) * fade * 1.6f);
    }

    // TEST composite: where the G-buffer has geometry, light the albedo with
    // the sun (Lambert) and the ambient term. The SSAO darkens only the
    // ambient term: it is the visibility of the sky, not of the sun.
    if (depth.SampleLevel(input.uv, 0.0f).r < 1.0f) {
      float3 base = albedo.SampleLevel(input.uv, 0.0f).rgb;
      float3 N = normalize(normal.SampleLevel(input.uv, 0.0f).xyz);
      float visibility = ssao.SampleLevel(input.uv, 0.0f).r;
      visibility = pow(visibility, ssaoContrast);


      float sun = max(dot(N, normalize(kSunDirection)), 0.0f);
      color = base * (kSunColor * sun + kAmbientColor * visibility);
    }

    return float4(color, 1.0f);
  }
}
