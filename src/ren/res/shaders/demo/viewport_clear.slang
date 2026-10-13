// Fullscreen sky and world-space grid.

import ren.core;

struct Varying {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

[[vk::push_constant]]
cbuffer pc {
  TextureHandle skyboxHdri;
}

// The standard vertexless fullscreen triangle: three vertices covering the
// whole viewport, with UVs derived from the vertex index rather than a buffer.
[shader("vertex")]
Varying vertexMain(uint vertexIndex: SV_VertexID) {
  Varying output;
  output.uv = float2((vertexIndex << 1) & 2, vertexIndex & 2);
  output.position = float4(output.uv * 2.0f - 1.0f, 0.0f, 1.0f);
  return output;
}

// Unity-style procedural sky. Pass a normalized world-space direction.
// Adjust these colors and the sun direction to change the appearance.
float3 unityEditorSky(float3 rayDir) {
  const float3 zenithColor = float3(0.16f, 0.35f, 0.65f);
  const float3 horizonColor = float3(0.72f, 0.78f, 0.82f);
  const float3 groundColor = float3(0.18f, 0.17f, 0.15f);
  const float3 sunColor = float3(1.0f, 0.92f, 0.75f);
  float3 sunDirection = normalize(float3(-0.4f, 0.6f, -0.7f));

  // Keep the horizon pale, then blend into blue above and ground below.
  float skyBlend = pow(saturate(rayDir.y), 0.45f);
  float groundBlend = smoothstep(0.0f, 0.25f, -rayDir.y);
  float3 color = lerp(horizonColor, zenithColor, skyBlend);
  color = lerp(color, groundColor, groundBlend);

  // A small sun disc and a broad halo, visible only above the horizon.
  float sunAlignment = saturate(dot(rayDir, sunDirection));
  float sunDisc = smoothstep(0.9998f, 0.99995f, sunAlignment);
  float sunHalo = pow(sunAlignment, 64.0f) * 0.25f;
  color += sunColor * (sunDisc + sunHalo) * smoothstep(0.0f, 0.02f, rayDir.y);
  return color;
}

[shader("fragment")]
float4 fragmentMain(Varying input) : SV_Target {
  // Reconstruct the world-space ray through this pixel, which is what makes
  // the image respond to where the camera is *pointing* and not just where it
  // is.
  //
  // The ray is built in view space and then rotated into world space. Doing it
  // in that order avoids picking basis vectors out of the matrix by hand,
  // which is easy to get subtly wrong: a mis-transposed rotation still
  // produces a plausible moving image, but pitch shows up as roll.
  float2 ndc = input.uv * 2.0f - 1.0f;

  // The projection's diagonal encodes the field of view: proj[0][0] is
  // 1/(aspect*tan(fovY/2)) and proj[1][1] is 1/tan(fovY/2). Dividing by them
  // converts the NDC offsets into view-space slopes, so the ray tracks
  // whatever FOV and aspect ratio the viewport actually set.
  //
  // projMatrix[1][1] carries the Vulkan Y flip. Keeping its sign is what makes
  // the vertical axis come out the right way up, so it is deliberately not
  // stripped with abs().
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
  float3 viewTranslation = float3(frameGlobals.viewMatrix[0][3],
                                  frameGlobals.viewMatrix[1][3],
                                  frameGlobals.viewMatrix[2][3]);
  float3 cameraPos = -mul(transpose(viewRotation), viewTranslation);

  // Map world direction to the equirectangular texture. The image center
  // faces -Z, its top faces +Y, and its bottom faces -Y.
  const float invPi = 0.318309886f;
  float2 skyUV = float2(0.5f + atan2(rayDir.x, -rayDir.z) * (0.5f * invPi),
                        acos(clamp(rayDir.y, -1.0f, 1.0f)) * invPi);
  float3 color = skyboxHdri.Sample(skyUV).rgb;
  // Uncomment to use the procedural sky instead of the HDRI.
  // color = unityEditorSky(rayDir);

  // Unit grid on the y = 0 plane, with thicker lines every ten cells.
  // Fade it out by 60 units and suppress lines smaller than one pixel.
  if (abs(rayDir.y) > 0.001f) {
    float t = -cameraPos.y / rayDir.y;
    if (t > 0.0f) {
      float2 hit = (cameraPos + rayDir * t).xz;
      float2 width = max(fwidth(hit), float2(0.0001f));
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
      float3 axisColor = (float3(1.0f, 0.1f, 0.1f) * xAxis +
                          float3(0.1f, 0.35f, 1.0f) * zAxis) / max(xAxis + zAxis, 0.0001f);
      color = lerp(color, axisColor, max(xAxis, zAxis) * fade * 1.6f);
    }
  }


  return float4(color, 1.0f);
}
