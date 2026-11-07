// Fullscreen sky, ground grid, and a procedural 3D sphere lattice.

import ren.core;

struct Varying {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

[[vk::push_constant]]
cbuffer pc {
  TextureHandle skyboxHdri;
  uint raysPerPixel;
  float focusDistance;
  float lensRadius;
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

// Stable per-cell values. A different salt selects a different property.
uint hashCell(int3 cell, uint salt) {
  uint3 v = uint3(cell);
  uint h = v.x * 0x9e3779b9u ^ v.y * 0x85ebca6bu ^ v.z * 0xc2b2ae35u ^ salt;
  h ^= h >> 16;
  h *= 0x7feb352du;
  h ^= h >> 15;
  h *= 0x846ca68bu;
  return h ^ (h >> 16);
}

float randomCell(int3 cell, uint salt) {
  return float(hashCell(cell, salt) & 0x00ffffffu) / 16777216.0f;
}

// A ray sees the nearest sphere or the ground plane on every step.
struct SceneHit {
  float t;
  int kind; // -1: sky, 0: sphere, 2: ground
  float3 position;
  float3 normal;
  int3 cell;
};

SceneHit traceScene(float3 origin, float3 direction) {
  SceneHit hit;
  hit.t = 1e20f;
  hit.kind = -1;
  hit.position = float3(0.0f);
  hit.normal = float3(0.0f);
  hit.cell = int3(0);

  if (abs(direction.y) > 0.001f) {
    float t = -origin.y / direction.y;
    if (t > 0.001f) {
      hit.t = t;
      hit.kind = 2;
      hit.position = origin + direction * t;
      hit.normal = float3(0.0f, 1.0f, 0.0f);
    }
  }

  // Cell centers within 50 units hold spheres. Allow their motion and
  // radius when finding the ray's search bound. The plane stays unbounded.
  float b = dot(origin, direction);
  float boundDiscriminant = b * b - (dot(origin, origin) - 52.0f * 52.0f);
  if (boundDiscriminant < 0.0f) return hit;
  float boundRoot = sqrt(boundDiscriminant);
  float boundNear = max(0.0f, -b - boundRoot);
  float boundFar = -b + boundRoot;
  if (boundFar <= 0.001f || hit.t <= boundNear) return hit;

  // Each cell has one moving sphere. Its radius and offset keep it
  // inside the cell, so DDA visits possible hits in distance order.
  // Start at the ball entry and choose the forward cell at a boundary.
  float3 cell = floor((origin + direction * (boundNear + 0.001f) + 1.5f) / 3.0f);
  float3 step = float3(direction.x >= 0.0f ? 1.0f : -1.0f,
                       direction.y >= 0.0f ? 1.0f : -1.0f,
                       direction.z >= 0.0f ? 1.0f : -1.0f);
  float3 delta = float3(direction.x != 0.0f ? abs(3.0f / direction.x) : 1e20f,
                        direction.y != 0.0f ? abs(3.0f / direction.y) : 1e20f,
                        direction.z != 0.0f ? abs(3.0f / direction.z) : 1e20f);
  float3 nextT = float3(direction.x != 0.0f ? ((cell.x + 0.5f * step.x) * 3.0f - origin.x) / direction.x : 1e20f,
                        direction.y != 0.0f ? ((cell.y + 0.5f * step.y) * 3.0f - origin.y) / direction.y : 1e20f,
                        direction.z != 0.0f ? ((cell.z + 0.5f * step.z) * 3.0f - origin.z) / direction.z : 1e20f);

  // Traverse cells until the ray exits the ball or hits the ground.
  for (int traversal = 0; traversal < 128; ++traversal) {
    float exitT = min(nextT.x, min(nextT.y, nextT.z));
    int3 sphereCell = int3(cell);
    float3 phase = 6.2831853f * float3(randomCell(sphereCell, 6u),
                                       randomCell(sphereCell, 7u),
                                       randomCell(sphereCell, 8u));
    float time = frameGlobals.time;
    float3 offsetInCell = 0.35f * float3(sin(time * 0.65f + phase.x),
                                         cos(time * 0.80f + phase.y),
                                         sin(time * 0.70f + phase.z));
    float3 center = cell * 3.0f + offsetInCell;
    float radius = 0.55f + 0.45f * randomCell(sphereCell, 1u);
    // Use the fixed cell center so motion never removes a sphere.
    float3 cellCenter = cell * 3.0f;
    if (dot(cellCenter, cellCenter) <= 50.0f * 50.0f) {
      float3 offset = origin - center;
      float halfB = dot(offset, direction);
      float discriminant = halfB * halfB - (dot(offset, offset) - radius * radius);
      if (discriminant >= 0.0f) {
        float root = sqrt(discriminant);
        float tNear = -halfB - root;
        float t = tNear > 0.001f ? tNear : -halfB + root;
        if (t > 0.001f && t <= exitT && t < hit.t) {
          hit.t = t;
          hit.kind = 0;
          hit.position = origin + direction * t;
          hit.normal = normalize(hit.position - center);
          hit.cell = sphereCell;
          return hit;
        }
      }
    }
    if (exitT >= min(hit.t, boundFar)) break;
    if (nextT.x <= exitT) { cell.x += step.x; nextT.x += delta.x; }
    if (nextT.y <= exitT) { cell.y += step.y; nextT.y += delta.y; }
    if (nextT.z <= exitT) { cell.z += step.z; nextT.z += delta.z; }
  }
  return hit;
}


float3 sampleSky(float3 direction) {
  const float invPi = 0.318309886f;
  float2 uv = float2(0.5f + atan2(direction.x, -direction.z) * (0.5f * invPi),
                     acos(clamp(direction.y, -1.0f, 1.0f)) * invPi);
  return skyboxHdri.SampleLevel(uv, 0.0f).rgb;
}

float3 shadeGround(SceneHit hit, float3 direction, float3 rayDx, float3 rayDy) {
  float3 color = sampleSky(direction);
  float2 pos = hit.position.xz;
  // Differentiate the ray-plane hit. Carry an approximate ray differential
  // through each sphere reflection to filter the grid on later hits too.
  float2 dx = (hit.t * (rayDx - direction * (rayDx.y / direction.y))).xz;
  float2 dy = (hit.t * (rayDy - direction * (rayDy.y / direction.y))).xz;
  float2 width = max(abs(dx) + abs(dy), float2(0.0001f));
  float2 minorDistance = abs(frac(pos + 0.5f) - 0.5f);
  float2 majorDistance = abs(frac(pos / 10.0f + 0.5f) - 0.5f) * 10.0f;
  float2 minorLines = 1.0f - smoothstep(float2(0.0f), width, minorDistance);
  float2 majorLines = 1.0f - smoothstep(float2(0.0f), 2.0f * width, majorDistance);
  float pixelFootprint = max(width.x, width.y);
  float minor = max(minorLines.x, minorLines.y) * saturate(1.0f - pixelFootprint);
  float major = max(majorLines.x, majorLines.y) * saturate(1.0f - pixelFootprint / 10.0f);
  float fade = saturate(1.0f - hit.t / 60.0f);
  color = lerp(color, float3(0.85f), max(minor, major) * fade * 0.6f);

  // The X axis lies along z = 0; the Z axis lies along x = 0.
  float axisVisibility = saturate(1.0f - pixelFootprint / 10.0f);
  float xAxis = (1.0f - smoothstep(0.0f, 2.0f * width.y, abs(pos.y))) * axisVisibility;
  float zAxis = (1.0f - smoothstep(0.0f, 2.0f * width.x, abs(pos.x))) * axisVisibility;
  float3 axisColor = (float3(1.0f, 0.1f, 0.1f) * xAxis +
                      float3(0.1f, 0.35f, 1.0f) * zAxis) / max(xAxis + zAxis, 0.0001f);
  return lerp(color, axisColor, max(xAxis, zAxis) * fade * 1.6f);
}

// Reusable path loop. A sphere reflects the ray; a ground or sky hit ends it.
float3 traceRay(float3 origin, float3 direction, float3 rayDx, float3 rayDy, uint pixelSeed) {
  float3 radiance = float3(0.0f);
  float3 throughput = float3(1.0f);
  const float reflectivity = 0.7f;
  for (int bounce = 0; bounce < 3; ++bounce) {
    SceneHit hit = traceScene(origin, direction);
    if (hit.kind < 0) return radiance + throughput * sampleSky(direction);
    if (hit.kind == 2) return radiance + throughput * shadeGround(hit, direction, rayDx, rayDy);

    float3 rawColor = float3(randomCell(hit.cell, 2u),
                             randomCell(hit.cell, 3u),
                             randomCell(hit.cell, 4u));
    float low = min(rawColor.x, min(rawColor.y, rawColor.z));
    float high = max(rawColor.x, max(rawColor.y, rawColor.z));
    // Keep each hue, but spread its channels to give it more saturation.
    float3 sphereColor = 0.1f + 0.9f * (rawColor - low) / max(high - low, 0.001f);
    // Cubing the uniform sample makes smooth spheres common and rough ones rare.
    float roughnessSample = randomCell(hit.cell, 5u);
    float roughness = 0.01f + 0.54f * roughnessSample * roughnessSample * roughnessSample;
    float light = 0.18f + 0.82f * max(dot(hit.normal, normalize(float3(0.4f, 1.0f, 0.3f))), 0.0f);
    radiance += throughput * sphereColor * light * (1.0f - reflectivity);
    throughput *= reflectivity;

    float3 mirrorDir = reflect(direction, hit.normal);
    // The roughness sample depends on the path, not on time.
    uint seed = hashCell(hit.cell, pixelSeed ^ uint(bounce) * 0x9e3779b9u);
    float angle = 6.2831853f * randomCell(hit.cell, seed);
    float radius = roughness * sqrt(randomCell(hit.cell, seed ^ 0x85ebca6bu));
    float3 helper = abs(mirrorDir.y) < 0.9f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 tangent = normalize(cross(helper, mirrorDir));
    float3 bitangent = cross(mirrorDir, tangent);
    float3 scattered = normalize(mirrorDir + radius * (cos(angle) * tangent + sin(angle) * bitangent));
    direction = dot(scattered, hit.normal) > 0.0f ? scattered : mirrorDir;
    rayDx = reflect(rayDx, hit.normal);
    rayDy = reflect(rayDy, hit.normal);
    origin = hit.position + hit.normal * 0.001f;
  }
  return radiance + throughput * sampleSky(direction);
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


  // Compute these before paths branch. Random offsets must not affect the
  // derivatives used to filter the grid.
  float3 rayDx = ddx(rayDir);
  float3 rayDy = ddy(rayDir);
  uint2 pixel = uint2(input.position.xy);
  uint frameSalt = frameGlobals.frameNumber * 0x9e3779b9u;
  float3 color = float3(0.0f);
  for (int sampleIndex = 0; sampleIndex < raysPerPixel; ++sampleIndex) {
    int3 sampleCell = int3(pixel.x, pixel.y, sampleIndex);
    // A new sample covers the full pixel on each frame.
    float2 jitter = float2(randomCell(sampleCell, 10u ^ frameSalt),
                           randomCell(sampleCell, 11u ^ frameSalt)) - 0.5f;
    float2 sampleNdc = ndc + jitter * (2.0f * frameGlobals.inverseRenderSize);
    float3 sampleViewRay = float3(sampleNdc.x / frameGlobals.projMatrix[0][0],
                                   sampleNdc.y / frameGlobals.projMatrix[1][1], -1.0f);
    float3 focusPoint = cameraPos + mul(transpose(viewRotation), sampleViewRay * focusDistance);

    float angle = 6.2831853f * randomCell(sampleCell, 12u ^ frameSalt);
    float radius = lensRadius * sqrt(randomCell(sampleCell, 13u ^ frameSalt));
    float2 lens = radius * float2(cos(angle), sin(angle));
    float3 origin = cameraPos + mul(transpose(viewRotation), float3(lens, 0.0f));
    float3 direction = normalize(focusPoint - origin);
    uint pathSeed = hashCell(sampleCell, 14u ^ frameSalt);
    color += traceRay(origin, direction, rayDx, rayDy, pathSeed);
  }
  color /= float(raysPerPixel);

  // float2 fromCenter = abs(input.uv - 0.5f) * frameGlobals.renderSize;
  // if ((fromCenter.x < 1.0f && fromCenter.y < 12.0f) ||
  //     (fromCenter.y < 1.0f && fromCenter.x < 12.0f)) {
  //   color = float3(1.0f, 1.0f, 1.0f);
  // }

  // float halfSize = 100.0f;
  // float2 center = float2(frameGlobals.renderSize.x - halfSize, halfSize);
  // float2 pixel = input.position.xy;
  // if (abs(pixel.x - center.x) <= halfSize && abs(pixel.y - center.y) <= halfSize) {
  //   float3 axes[3] = {float3(1.0f, 0.0f, 0.0f),
  //                     float3(0.0f, 1.0f, 0.0f),
  //                     float3(0.0f, 0.0f, 1.0f)};
  //   float3 axisColors[3] = {float3(1.0f, 0.15f, 0.15f),
  //                           float3(0.2f, 0.9f, 0.2f),
  //                           float3(0.2f, 0.45f, 1.0f)};
  //   for (int i = 0; i < 3; ++i) {
  //     float3 viewAxis = mul(viewRotation, axes[i]);
  //     float2 end = center + float2(viewAxis.x, -viewAxis.y) * (halfSize * 0.75f);
  //     float2 segment = end - center;
  //     float along = saturate(dot(pixel - center, segment) /
  //                            max(dot(segment, segment), 0.0001f));
  //     float distanceToAxis = length(pixel - (center + segment * along));
  //     float outline = 1.0f - smoothstep(3.0f, 4.0f, distanceToAxis);
  //     float line = 1.0f - smoothstep(1.5f, 2.5f, distanceToAxis);
  //     color = lerp(color, float3(0.05f), outline * 0.8f);
  //     color = lerp(color, axisColors[i], line);
  //   }
  // }

  return float4(color, 1.0f);
}


