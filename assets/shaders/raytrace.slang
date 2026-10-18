import ren.core;
import ren.surface;

[[vk::push_constant]]
cbuffer root {
  uint numSpheres;
  Sphere *spheres;
}

export struct Surface : IFullscreenSurface = RaytraceFullScreenSurface;

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


    return float4(calculateRayCollision(ray).material.color.rgb, 1.0f);

    // if (closestHit.didHit) {
    //   return float4(closestHit.material.color.rgb, 1.0f);
    // }
    // return float4(ray.dir * 0.5f + 0.5f, 1.0f);
  }
}
