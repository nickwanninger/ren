// A trivial fullscreen pass used to prove out the Viewport abstraction.
//
// It draws no geometry. Every pixel is derived from `frameGlobals` alone,
// which is the point: each Viewport binds its *own* FrameGlobals buffer before
// running its graph, so two viewports with different cameras must produce
// visibly different images. If both viewports render the same picture, the
// per-viewport binding is not taking effect.

import ren.core;

struct Varying {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

// The standard vertexless fullscreen triangle: three vertices covering the
// whole viewport, with UVs derived from the vertex index rather than a buffer.
[shader("vertex")]
Varying vertexMain(uint vertexIndex: SV_VertexID) {
  Varying output;
  output.uv = float2((vertexIndex << 1) & 2, vertexIndex & 2);
  output.position = float4(output.uv * 2.0f - 1.0f, 0.0f, 1.0f);
  return output;
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

  // The camera's world position, recovered by inverting the view transform.
  float3 cameraPos = -mul(transpose(viewRotation), frameGlobals.viewMatrix[3].xyz);

  // A procedural sky, so the picture is anchored to world space: the horizon
  // stays level and the sun stays put as the camera turns, which is exactly
  // what proves the rotation is reaching the shader.
  const float3 skyColor = float3(0.35f, 0.55f, 0.85f);
  const float3 groundColor = float3(0.22f, 0.20f, 0.18f);
  const float3 horizonColor = float3(0.75f, 0.78f, 0.80f);

  // Blend across the horizon over a few degrees rather than a hard cut, so
  // the seam does not alias into a stair-stepped line.
  float horizon = smoothstep(-0.03f, 0.03f, rayDir.y);
  float3 color = lerp(groundColor, skyColor, horizon);
  color = lerp(horizonColor, color, smoothstep(0.0f, 0.25f, abs(rayDir.y)));

  // A fixed world-space sun. Because it is defined by direction alone it sits
  // at infinity, so it moves when the camera rotates but not when it dollies.
  const float3 sunDir = normalize(float3(0.4f, 0.35f, -0.85f));
  float sun = max(dot(rayDir, sunDir), 0.0f);
  color += float3(1.0f, 0.95f, 0.8f) * pow(sun, 256.0f);
  color += float3(0.6f, 0.55f, 0.4f) * pow(sun, 8.0f) * 0.25f;

  // A world-space grid on the ground plane, which gives the translation
  // something to slide against -- rotation alone would leave a sky that looks
  // identical from every position.
  if (rayDir.y < -0.001f) {
    // Distance along the ray to y == 0, from the camera's height.
    float t = -cameraPos.y / rayDir.y;
    if (t > 0.0f) {
      float3 hit = cameraPos + rayDir * t;
      float2 cell = abs(frac(hit.xz) - 0.5f);
      float line = 1.0f - smoothstep(0.0f, 0.03f, min(cell.x, cell.y));
      // Fade the grid out with distance so the far field does not turn into
      // moire where the lines are closer together than the pixels.
      float fade = saturate(1.0f - t / 60.0f);
      color = lerp(color, float3(0.85f), line * fade * 0.6f);
    }
  }

  // // A crosshair marking the view center, so the aim direction is readable
  // // even when pointed at featureless sky.
  // float2 fromCenter = abs(input.uv - 0.5f) * frameGlobals.renderSize;
  // if ((fromCenter.x < 1.0f && fromCenter.y < 12.0f) ||
  //     (fromCenter.y < 1.0f && fromCenter.x < 12.0f)) {
  //   color = float3(1.0f, 1.0f, 1.0f);
  // }

  return float4(color, 1.0f);
}
