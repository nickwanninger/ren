import ren.core;
import ren.surface;

static const float PI = 3.14159;

[[vk::push_constant]]
cbuffer root {
  uint randomSeed;
  uint numSpheres;
  uint maxBounces;
  uint raysPerPixel;
  // Frames already in the running average. 0 starts a new average.
  uint accumulatedFrames;
  Sphere *spheres;
  // One running average per pixel, row by row.
  float4 *accumulation;

  TextureHandle skyboxHdri;
}

struct Ray {
  // Current position and direction of the ray
  // (inverse dir cached to speed up collision tests)
  float3 pos;
  float3 dir;
  float3 invDir;
  // Number of times the path has collided with an object
  int bounceCount;
};

struct HitInfo {
  bool didHit;
  float dst;
  float3 hitPoint;
  float3 normal;
  RayTracingMaterial material;
}

struct RayTracingMaterial {
  float4 color;
  float4 emissionColor;
}

struct Sphere {
  float3 position;
  float radius;
  RayTracingMaterial material;

  HitInfo intersect(float3 rayPos, float3 rayDir) {
    HitInfo hitInfo = (HitInfo)0;

    float3 offsetRayOrigin = rayPos - position;
    // From the equation: sqrLength(offsetRayOrigin + ray.dir * t) = radius^2
    float a = dot(rayDir, rayDir); // a = 1 assuming unit vector.
    float b = 2 * dot(offsetRayOrigin, rayDir);
    float c = dot(offsetRayOrigin, offsetRayOrigin) - radius * radius;
    float discriminant = b * b - 4 * a * c;
    if (discriminant >= 0) {
      float dst = (-b - sqrt(discriminant)) / (2 * a);
      if (dst > 0) {
        hitInfo.didHit = true;
        hitInfo.dst = dst;
        hitInfo.hitPoint = rayPos + rayDir * dst;
        hitInfo.normal = normalize(hitInfo.hitPoint - position);
        hitInfo.material = material;
      }
    }

    return hitInfo;
  }
}

HitInfo calculateRayCollision(Ray ray) {
  HitInfo closestHit = (HitInfo)0;
  closestHit.didHit = false;

  for (uint i = 0; i < numSpheres; i++) {
    Sphere sphere = spheres[i];
    HitInfo hit = sphere.intersect(ray.pos, ray.dir);
    if (hit.didHit && (closestHit.didHit == false || hit.dst < closestHit.dst)) {
      closestHit = hit;
    }
  }

  return closestHit;
}

// PCG (permuted congruential generator). Thanks to:
// www.pcg-random.org and www.shadertoy.com/view/XlGcRh
uint NextRandom(inout uint state) {
  state = state * 747796405 + 2891336453;
  uint result = ((state >> ((state >> 28) + 4)) ^ state) * 277803737;
  result = (result >> 22) ^ result;
  return result;
}

float RandomValue(inout uint state) {
  return NextRandom(state) / 4294967295.0; // 2^32 - 1
}

// Random value in normal distribution (with mean=0 and sd=1)
float RandomValueNormalDistribution(inout uint state) {
  // Thanks to https://stackoverflow.com/a/6178290
  float theta = 2 * 3.1415926 * RandomValue(state);
  float rho = sqrt(-2 * log(RandomValue(state)));
  return rho * cos(theta);
}

// Calculate a random direction
float3 RandomDirection(inout uint state) {
  // Thanks to https://math.stackexchange.com/a/1585996
  float x = RandomValueNormalDistribution(state);
  float y = RandomValueNormalDistribution(state);
  float z = RandomValueNormalDistribution(state);
  return normalize(float3(x, y, z));
}

float2 RandomPointInCircle(inout uint rngState) {
  float angle = RandomValue(rngState) * 2 * PI;
  float2 pointOnCircle = float2(cos(angle), sin(angle));
  return pointOnCircle * sqrt(RandomValue(rngState));
}

// Calculate a random direction
float3 RandomHemisphereDirection(float3 normal, inout uint state) {
  float3 dir = RandomDirection(state);
  return dir * sign(dot(normal, dir));
}

float3 GetEnvironmentLight(float3 dir) {

  const float invPi = 0.318309886f;
  float2 skyUV = float2(0.5f + atan2(dir.x, -dir.z) * (0.5f * invPi), acos(clamp(dir.y, -1.0f, 1.0f)) * invPi);

  float3 color = skyboxHdri.Sample(skyUV).rgb;
  return color;
}

float3 Trace(Ray ray, inout uint rngState) {
  float3 incomingLight = 0.0f;
  float3 rayColor = 1.0f;

  for (uint i = 0; i < maxBounces; i++) {
    HitInfo hitInfo = calculateRayCollision(ray);
    if (hitInfo.didHit) {
      ray.pos = hitInfo.hitPoint + hitInfo.normal * 1e-4f; // Offset to avoid self-intersection
      ray.dir = RandomHemisphereDirection(hitInfo.normal, rngState);
      ray.invDir = 1.0f / ray.dir;

      float3 emittedLight = hitInfo.material.emissionColor.rgb * hitInfo.material.emissionColor.a;
      incomingLight += rayColor * emittedLight;
      rayColor *= hitInfo.material.color.rgb;
    } else {
      incomingLight += GetEnvironmentLight(ray.dir) * rayColor;
      break;
    }
  }
  return incomingLight;
}

export struct Surface : IFullscreenSurface = RaytraceFullScreenSurface;
struct RaytraceFullScreenSurface : IFullscreenSurface {
  static float4 shade(FullscreenVarying input) {
    float2 ndc = input.uv * 2.0f - 1.0f;
    float3 viewRay = float3(ndc.x / frameGlobals.projMatrix[0][0], ndc.y / frameGlobals.projMatrix[1][1], -1.0f);
    float3x3 viewRotation = (float3x3)frameGlobals.viewMatrix;
    viewRay = normalize(mul(transpose(viewRotation), viewRay));

    // The view matrix maps world to view space: v = R * p + t. The camera is
    // the view-space origin, so 0 = R * cameraPos + t, and
    // cameraPos = -transpose(R) * t. The translation is column 3; matrix
    // indices here select rows.
    float3 viewTranslation =
        float3(frameGlobals.viewMatrix[0][3], frameGlobals.viewMatrix[1][3], frameGlobals.viewMatrix[2][3]);

    Ray ray;
    ray.pos = -mul(transpose(viewRotation), viewTranslation);
    ray.dir = viewRay;
    ray.invDir = 1.0f / ray.dir;
    ray.bounceCount = 0;

    // The fullscreen triangle runs each pixel once, so each fragment owns
    // one entry of the accumulation buffer.
    uint2 size = uint2(frameGlobals.renderSize);
    uint2 pixel = min(uint2(input.uv * frameGlobals.renderSize), size - 1);
    uint index = pixel.y * size.x + pixel.x;

    // Seed from the pixel index, not the UV: UV-based seeds repeat across
    // pixels, and that pattern does not average out.
    uint state = index + randomSeed * 719393u;

    float3 totalIncomingLight = 0.0f;
    for (uint i = 0; i < raysPerPixel; i++) {

      Ray jitteredRay = ray;
      float2 jitter = RandomPointInCircle(state) / float2(size);
      jitteredRay.dir = normalize(jitteredRay.dir + float3(jitter, 0.0f));
      jitteredRay.invDir = 1.0f / jitteredRay.dir;
      totalIncomingLight += Trace(jitteredRay, state);
    }
    float3 color = totalIncomingLight / raysPerPixel;

    // Running average: avg += (new - avg) / (n + 1).
    if (accumulatedFrames > 0) {
      float3 average = accumulation[index].rgb;
      color = average + (color - average) / float(accumulatedFrames + 1);
    }
    accumulation[index] = float4(color, 1.0f);

    return float4(color, 1.0f);
  }
}
