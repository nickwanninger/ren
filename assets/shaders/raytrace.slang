import ren.core;
import ren.surface;
import ren.core.random;
import ren.core.bvh;

[[vk::push_constant]]
cbuffer root {
  uint randomSeed;
  uint numSpheres;
  uint maxBounces;
  uint raysPerPixel;
  // Frames already in the running average. 0 starts a new average.
  uint accumulatedFrames;
  // 1 samples emissive spheres directly at each diffuse hit (next event
  // estimation). 0 finds lights only with bounce rays.
  uint lightSampling;
  Sphere *spheres;
  // One running average per pixel, row by row.
  float4 *accumulation;

  TextureHandle skyboxHdri;

  // From the megamesh buffer, which is a single buffer that contains all the vertex and index data for all meshes.
  Vertex *vertices;
  uint *indices;
  uint numMeshes;
  Mesh *meshes;

  uint debugView;      // 0 = none, 1 = triangle tests, 2 = bounds tests
  uint debugTestScale; // Any value lower than this goes from black to white. higher values are set to pure red.
}

struct DebugInfo {
  uint boundsTests;
  uint triangleTests;
}

struct Ray {
  // Current position and direction of the ray
  // (inverse dir cached to speed up collision tests)
  float3 pos;
  float3 dir;
  float3 invDir;
  // Number of times the path has collided with an object
  int bounceCount;
}

static const float kPi = 3.14159265f;
static const float kInvPi = 0.318309886f;

struct HitInfo {
  bool didHit;
  float dst;
  float3 hitPoint;
  float3 normal;
  float2 uv;
  RayTracingMaterial material;
  // Index into spheres, or -1 when the hit is not a sphere.
  int sphereIndex;
}

struct RayTracingMaterial {
  float4 color;
  float4 emissionColor;
  float smoothness;
  float _pad0;
  float _pad1;
  float _pad2;
}

struct Sphere {
  float3 position;
  float radius;
  RayTracingMaterial material;

  HitInfo intersect(float3 rayPos, float3 rayDir, inout DebugInfo dbg) {
    dbg.boundsTests++;
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

// Ray-triangle intersection (thanks to stackoverflow.com/a/42752998)
// a, b and c are absolute indices into the vertex buffer.
HitInfo RayTriangle(Ray ray, uint a, uint b, uint c, bool cullBackface) {
  Vertex vertA = vertices[a];
  Vertex vertB = vertices[b];
  Vertex vertC = vertices[c];

  float3 edgeAB = vertB.position - vertA.position;
  float3 edgeAC = vertC.position - vertA.position;
  float3 triFaceVector = cross(edgeAB, edgeAC);
  float3 vertRayOffset = ray.pos - vertA.position;
  float3 rayOffsetPerp = cross(vertRayOffset, ray.dir);
  float determinant = -dot(ray.dir, triFaceVector);
  float invDet = 1 / determinant;

  // Calculate hit-dst and barycentric coordinates
  float dst = dot(vertRayOffset, triFaceVector) * invDet;
  float u = dot(edgeAC, rayOffsetPerp) * invDet;
  float v = -dot(edgeAB, rayOffsetPerp) * invDet;
  float w = 1 - u - v;

  // Initialize hit info
  HitInfo hitInfo = (HitInfo)0;
  bool keep = cullBackface ? determinant >= 1E-8 : abs(determinant) >= 1E-8;
  hitInfo.didHit = keep && dst > 0 && u >= 0 && v >= 0 && w >= 0;
  if (!hitInfo.didHit) {
    return hitInfo;
  }


  float3 smoothNormal = normalize(vertA.normal * w + vertB.normal * u + vertC.normal * v);
  hitInfo.normal = smoothNormal * sign(determinant);
  hitInfo.uv = vertA.texCoord * w + vertB.texCoord * u + vertC.texCoord * v;
  // hitInfo.normal = normalize(triFaceVector);
  // hitInfo.isBackface = determinant < 0;
  hitInfo.hitPoint = ray.pos + ray.dir * dst;
  hitInfo.dst = dst;
  return hitInfo;
}

// Triangle test for MeshBVH.traverse. Keeps the closest hit.
struct MeshTriangleTest : IBVHTriangleTest {
  // In model space.
  Ray ray;
  uint indexOffset;
  uint vertexOffset;
  HitInfo hit;
  uint triangleTests;

  [mutating]
  void test(uint triangle, inout float tMax) {
    triangleTests++;
    uint first = indexOffset + triangle * 3;
    HitInfo candidate = RayTriangle(ray, vertexOffset + indices[first + 0], vertexOffset + indices[first + 1],
                                    vertexOffset + indices[first + 2], false);
    if (candidate.didHit && candidate.dst < tMax) {
      hit = candidate;
      tMax = candidate.dst;
    }
  }
}

struct Mesh {
  // Moves a world-space ray into the model space of the mesh.
  float4x4 worldToModel;
  // Bounding box in model space.
  float3 minBounds;
  // The triangles of the mesh in the index buffer.
  uint indexOffset;
  float3 maxBounds;
  uint indexCount;
  // The indices are relative to this vertex in the vertex buffer.
  uint vertexOffset;
  uint _pad;
  // Device addresses of the mesh BVH. These are not pointers because
  // SPIRV-Cross (MoltenVK) emits bad Metal for a copy of a struct that holds
  // pointers. Use getBVH().
  uint64_t bvhNodes;
  uint64_t bvhTriangles;
  uint64_t _pad2;
  RayTracingMaterial material;

  MeshBVH getBVH() {
    MeshBVH bvh;
    bvh.nodes = (BVHNode *)bvhNodes;
    bvh.triangles = (uint *)bvhTriangles;
    return bvh;
  }

  // Closest hit before tMax. didHit is false when there is no closer hit.
  HitInfo intersect(float3 rayPos, float3 rayDir, float tMax, inout DebugInfo dbg) {
    // Do not normalize the model-space direction. Then t is the same in
    // both spaces, and hits on different meshes stay comparable.
    MeshTriangleTest tester = (MeshTriangleTest)0;
    tester.ray.pos = mul(worldToModel, float4(rayPos, 1.0f)).xyz;
    tester.ray.dir = mul(worldToModel, float4(rayDir, 0.0f)).xyz;
    tester.ray.invDir = 1.0f / tester.ray.dir;
    tester.indexOffset = indexOffset;
    tester.vertexOffset = vertexOffset;

    // The BVH root is an inner node with no box of its own. Test the mesh box
    // first, so a miss costs one test and no node fetch.
    dbg.boundsTests++;
    if (IntersectBox(minBounds, maxBounds, tester.ray.pos, tester.ray.invDir, tMax) == kBVHMiss) {
      return (HitInfo)0;
    }

    dbg.boundsTests += getBVH().traverse(tester.ray.pos, tester.ray.dir, tMax, tester);
    dbg.triangleTests += tester.triangleTests;

    HitInfo hitInfo = tester.hit;
    if (hitInfo.didHit) {
      // RayTriangle works in model space. dst is the same in world space.
      hitInfo.hitPoint = rayPos + rayDir * hitInfo.dst;
      // Normals move to world space with the inverse transpose of the model
      // matrix, which is the transpose of worldToModel.
      hitInfo.normal = normalize(mul(transpose((float3x3)worldToModel), hitInfo.normal));
      hitInfo.material = material;
      hitInfo.material.color = float4(hitInfo.uv, 0.0f, 1.0f);
    }
    return hitInfo;
  }
}

HitInfo calculateRayCollision(Ray ray, inout DebugInfo dbg) {
  HitInfo closestHit = (HitInfo)0;
  closestHit.didHit = false;
  closestHit.sphereIndex = -1;

  for (uint i = 0; i < numSpheres; i++) {
    Sphere sphere = spheres[i];
    HitInfo hit = sphere.intersect(ray.pos, ray.dir, dbg);
    if (hit.didHit && (closestHit.didHit == false || hit.dst < closestHit.dst)) {
      closestHit = hit;
      closestHit.sphereIndex = int(i);
    }
  }

  for (uint i = 0; i < numMeshes; i++) {
    Mesh mesh = meshes[i];
    // The closest hit so far culls the parts of the BVH behind it.
    float tMax = closestHit.didHit ? closestHit.dst : kBVHMiss;
    HitInfo hit = mesh.intersect(ray.pos, ray.dir, tMax, dbg);
    if (hit.didHit && (closestHit.didHit == false || hit.dst < closestHit.dst)) {
      closestHit = hit;
      closestHit.sphereIndex = -1;
    }
  }

  return closestHit;
}

float3 GetEnvironmentLight(float3 dir) {
  // return float3(0);
  float2 skyUV = float2(0.5f + atan2(dir.x, -dir.z) * (0.5f * kInvPi), acos(clamp(dir.y, -1.0f, 1.0f)) * kInvPi);

  float3 color = skyboxHdri.SampleLevel(skyUV, 2).rgb;
  return color;
}

float3 Emission(RayTracingMaterial material) { return material.emissionColor.rgb * material.emissionColor.a; }

bool IsLight(Sphere sphere) { return any(Emission(sphere.material) > 0.0f); }

uint CountLights() {
  uint count = 0;
  for (uint i = 0; i < numSpheres; i++) {
    if (IsLight(spheres[i])) {
      count++;
    }
  }
  return count;
}

// Index into spheres of the n-th emissive sphere.
uint FindLight(uint n) {
  for (uint i = 0; i < numSpheres; i++) {
    if (IsLight(spheres[i])) {
      if (n == 0) {
        return i;
      }
      n--;
    }
  }
  return 0;
}

// 1 - cos(theta max) of the cone that the sphere fills, seen from pos.
// 0 when pos is inside the sphere.
float SphereConeSize(Sphere sphere, float3 pos) {
  float3 toCenter = sphere.position - pos;
  float sinThetaMaxSq = sphere.radius * sphere.radius / dot(toCenter, toCenter);
  if (sinThetaMaxSq >= 1.0f) {
    return 0.0f;
  }
  // Same as 1 - sqrt(1 - x), without the cancellation for small spheres.
  return sinThetaMaxSq / (1.0f + sqrt(1.0f - sinThetaMaxSq));
}

// Solid angle pdf of a uniform direction in the cone that the sphere fills.
float SphereConePdf(Sphere sphere, float3 pos) {
  float coneSize = SphereConeSize(sphere, pos);
  return coneSize > 0.0f ? 1.0f / (2.0f * kPi * coneSize) : 0.0f;
}

// Orthonormal basis around a unit vector n (Duff et al. 2017).
void BuildBasis(float3 n, out float3 t, out float3 b) {
  float s = n.z >= 0.0f ? 1.0f : -1.0f;
  float a = -1.0f / (s + n.z);
  float c = n.x * n.y * a;
  t = float3(1.0f + s * n.x * n.x * a, s * c, -s * n.x);
  b = float3(c, s + n.y * n.y * a, -n.y);
}

float PowerHeuristic(float pdfA, float pdfB) {
  float a = pdfA * pdfA;
  float b = pdfB * pdfB;
  return a + b > 0.0f ? a / (a + b) : 0.0f;
}

// Next event estimation for a Lambertian surface. Picks one emissive sphere
// and samples a direction in its cone. The result does not include the
// albedo, which the caller applies with the path throughput.
float3 SampleLights(float3 pos, float3 normal, uint numLights, inout uint rngState, inout DebugInfo dbg) {
  uint lightIndex = FindLight(min(uint(RandomValue(rngState) * numLights), numLights - 1));
  Sphere light = spheres[lightIndex];

  float coneSize = SphereConeSize(light, pos);
  if (coneSize <= 0.0f) {
    return 0.0f;
  }

  float3 w = normalize(light.position - pos);
  float3 t, b;
  BuildBasis(w, t, b);
  float cosTheta = 1.0f - RandomValue(rngState) * coneSize;
  float sinTheta = sqrt(max(0.0f, 1.0f - cosTheta * cosTheta));
  float phi = 2.0f * kPi * RandomValue(rngState);
  float3 dir = normalize(t * (sinTheta * cos(phi)) + b * (sinTheta * sin(phi)) + w * cosTheta);

  float cosSurface = dot(normal, dir);
  if (cosSurface <= 0.0f) {
    return 0.0f;
  }

  Ray shadowRay = (Ray)0;
  shadowRay.pos = pos;
  shadowRay.dir = dir;
  shadowRay.invDir = 1.0f / dir;
  HitInfo hit = calculateRayCollision(shadowRay, dbg);
  if (!hit.didHit || hit.sphereIndex != int(lightIndex)) {
    return 0.0f;
  }

  float lightPdf = 1.0f / (2.0f * kPi * coneSize * numLights);
  float bsdfPdf = cosSurface * kInvPi;
  // Lambertian: f * cos / pdf = (albedo / pi) * cos / lightPdf.
  return Emission(hit.material) * (cosSurface * kInvPi / lightPdf) * PowerHeuristic(lightPdf, bsdfPdf);
}

float3 Trace(Ray ray, uint numLights, inout uint rngState, inout DebugInfo dbg) {
  float3 incomingLight = 0.0f;
  float3 rayColor = 1.0f;

  // The previous diffuse bounce, for the MIS weight when the bounce ray hits
  // a light that next event estimation also sampled.
  bool lastBounceSampledLights = false;
  float3 lastPos = 0.0f;
  float lastBsdfPdf = 0.0f;

  for (uint i = 0; i <= maxBounces; i++) {
    HitInfo hitInfo = calculateRayCollision(ray, dbg);
    if (hitInfo.didHit) {
      let material = hitInfo.material;

      float3 emittedLight = Emission(material);
      if (any(emittedLight > 0.0f)) {
        float weight = 1.0f;
        if (lastBounceSampledLights) {
          float lightPdf = SphereConePdf(spheres[hitInfo.sphereIndex], lastPos) / numLights;
          weight = PowerHeuristic(lastBsdfPdf, lightPdf);
        }
        incomingLight += emittedLight * rayColor * weight;
      }

      // Offset to avoid self-intersection. This must be larger than
      // kBulbHitEpsilon, or the bounce ray hits the bulb at t = 0.
      ray.pos = hitInfo.hitPoint + hitInfo.normal * 1e-3f;

      float3 albedo = saturate(material.color.rgb);
      if (RandomValue(rngState) < material.smoothness) {
        // Mirror bounce. A delta lobe cannot use next event estimation.
        ray.dir = reflect(ray.dir, hitInfo.normal);
        lastBounceSampledLights = false;
      } else {
        lastBounceSampledLights = lightSampling != 0 && numLights > 0;
        if (lastBounceSampledLights) {
          incomingLight += SampleLights(ray.pos, hitInfo.normal, numLights, rngState, dbg) * albedo * rayColor;
        }
        // Cosine weighted random direction.
        ray.dir = normalize(hitInfo.normal + RandomDirection(rngState));
        lastPos = ray.pos;
        lastBsdfPdf = max(dot(hitInfo.normal, ray.dir), 0.0f) * kInvPi;
      }
      ray.invDir = 1.0f / ray.dir;
      rayColor *= albedo;
    } else {
      // Next event estimation does not sample the environment, so a bounce
      // ray is the only way to find it and it keeps the full weight.
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

    uint state = index + randomSeed * 719393u;

    DebugInfo dbg = (DebugInfo)0;
    uint numLights = CountLights();

    float3 totalIncomingLight = 0.0f;
    for (uint i = 0; i < raysPerPixel; i++) {

      Ray jitteredRay = ray;
      float2 jitter = RandomPointInCircle(state) / float2(size);
      jitteredRay.dir = normalize(jitteredRay.dir + float3(jitter, 0.0f));
      jitteredRay.invDir = 1.0f / jitteredRay.dir;
      totalIncomingLight += Trace(jitteredRay, numLights, state, dbg);
    }
    float3 color = totalIncomingLight / raysPerPixel;

    // Running average: avg += (new - avg) / (n + 1).
    if (accumulatedFrames > 0) {
      float3 average = accumulation[index].rgb;
      color = average + (color - average) / float(accumulatedFrames + 1);
    }
    accumulation[index] = float4(color, 1.0f);

    if (debugView != 0) {
      uint count = debugView == 1 ? dbg.triangleTests : dbg.boundsTests;
      // if the value is less than debugTestScale, map it to 0-1. If it's higher, make it pure red.
      float3 dbgColor = count < debugTestScale ? float3(float(count) / debugTestScale) : float3(1, 0, 0);
      return float4(dbgColor, 1.0f);
    }

    return float4(color, 1.0f);
  }
}
