import ren.core;
import ren.surface;
import ren.core.random;

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

  // From the megamesh buffer, which is a single buffer that contains all the vertex and index data for all meshes.
  Vertex *vertices;
  uint *indices;
  uint numMeshes;
  Mesh *meshes;
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
  float smoothness;
  // float3 _pad;
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
  float3 smoothNormal = normalize(vertA.normal * w + vertB.normal * u + vertC.normal * v);
  hitInfo.normal = smoothNormal * sign(determinant);
  // hitInfo.normal = normalize(triFaceVector);
  // hitInfo.isBackface = determinant < 0;
  hitInfo.hitPoint = ray.pos + ray.dir * dst;
  hitInfo.dst = dst;
  return hitInfo;
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
  uint _pad[3];

  HitInfo intersect(float3 rayPos, float3 rayDir, inout DebugInfo dbg) {
    HitInfo hitInfo = (HitInfo)0;

    // Do not normalize the model-space direction. Then t is the same in
    // both spaces, and hits on different meshes stay comparable.
    float3 modelPos = mul(worldToModel, float4(rayPos, 1.0f)).xyz;
    float3 modelDir = mul(worldToModel, float4(rayDir, 0.0f)).xyz;

    float3 invDir = 1.0f / modelDir;
    float3 t0s = (minBounds - modelPos) * invDir;
    float3 t1s = (maxBounds - modelPos) * invDir;
    float3 tsmaller = min(t0s, t1s);
    float3 tbigger = max(t0s, t1s);
    float tmin = max(max(tsmaller.x, tsmaller.y), tsmaller.z);
    float tmax = min(min(tbigger.x, tbigger.y), tbigger.z);
    dbg.boundsTests++;

    if (tmax < max(tmin, 0.0f)) {
      return hitInfo;
    }

    // No BVH: test every triangle of the mesh.
    Ray modelRay = (Ray)0;
    modelRay.pos = modelPos;
    modelRay.dir = modelDir;
    modelRay.invDir = invDir;
    for (uint i = 0; i + 2 < indexCount; i += 3) {
      uint a = vertexOffset + indices[indexOffset + i + 0];
      uint b = vertexOffset + indices[indexOffset + i + 1];
      uint c = vertexOffset + indices[indexOffset + i + 2];
      dbg.triangleTests++;
      HitInfo hit = RayTriangle(modelRay, a, b, c, false);
      if (hit.didHit && (hitInfo.didHit == false || hit.dst < hitInfo.dst)) {
        hitInfo = hit;
      }
    }

    if (hitInfo.didHit) {
      // RayTriangle works in model space. dst is the same in world space.
      hitInfo.hitPoint = rayPos + rayDir * hitInfo.dst;
      // Normals move to world space with the inverse transpose of the model
      // matrix, which is the transpose of worldToModel.
      hitInfo.normal = normalize(mul(transpose((float3x3)worldToModel), hitInfo.normal));
      // TODO: send a material per mesh.
      hitInfo.material.color = float4(0.8f, 0.8f, 0.8f, 1.0f);
    }
    return hitInfo;
  }
}

HitInfo calculateRayCollision(Ray ray, inout DebugInfo dbg) {
  HitInfo closestHit = (HitInfo)0;
  closestHit.didHit = false;

  for (uint i = 0; i < numSpheres; i++) {
    Sphere sphere = spheres[i];
    HitInfo hit = sphere.intersect(ray.pos, ray.dir, dbg);
    if (hit.didHit && (closestHit.didHit == false || hit.dst < closestHit.dst)) {
      closestHit = hit;
    }
  }

  for (uint i = 0; i < numMeshes; i++) {
    Mesh mesh = meshes[i];
    HitInfo hit = mesh.intersect(ray.pos, ray.dir, dbg);
    if (hit.didHit && (closestHit.didHit == false || hit.dst < closestHit.dst)) {
      closestHit = hit;
    }
  }

  return closestHit;
}

float3 GetEnvironmentLight(float3 dir) {
  // return float3(0);
  const float invPi = 0.318309886f;
  float2 skyUV = float2(0.5f + atan2(dir.x, -dir.z) * (0.5f * invPi), acos(clamp(dir.y, -1.0f, 1.0f)) * invPi);

  float3 color = skyboxHdri.Sample(skyUV).rgb;
  return color;
}

float3 Trace(Ray ray, inout uint rngState, inout DebugInfo dbg) {
  float3 incomingLight = 0.0f;
  float3 rayColor = 1.0f;

  for (uint i = 0; i <= maxBounces; i++) {
    HitInfo hitInfo = calculateRayCollision(ray, dbg);
    if (hitInfo.didHit) {
      // Offset to avoid self-intersection. This must be larger than
      // kBulbHitEpsilon, or the bounce ray hits the bulb at t = 0.
      ray.pos = hitInfo.hitPoint + hitInfo.normal * 1e-3f;

      let material = hitInfo.material;

      // Cosine weighted random direction.
      let diffuseDir = normalize(hitInfo.normal + RandomDirection(rngState));
      let specularDir = reflect(ray.dir, hitInfo.normal);

      ray.dir = normalize(lerp(diffuseDir, specularDir, material.smoothness));
      ray.invDir = 1.0f / ray.dir;

      float3 emittedLight = material.emissionColor.rgb * material.emissionColor.a;
      incomingLight += emittedLight * rayColor;
      rayColor *= saturate(material.color.rgb);
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

    uint state = index + randomSeed * 719393u;

    DebugInfo dbg = (DebugInfo)0;

    float3 totalIncomingLight = 0.0f;
    for (uint i = 0; i < raysPerPixel; i++) {

      Ray jitteredRay = ray;
      float2 jitter = RandomPointInCircle(state) / float2(size);
      jitteredRay.dir = normalize(jitteredRay.dir + float3(jitter, 0.0f));
      jitteredRay.invDir = 1.0f / jitteredRay.dir;
      totalIncomingLight += Trace(jitteredRay, state, dbg);
    }
    float3 color = totalIncomingLight / raysPerPixel;

    // Running average: avg += (new - avg) / (n + 1).
    if (accumulatedFrames > 0) {
      float3 average = accumulation[index].rgb;
      color = average + (color - average) / float(accumulatedFrames + 1);
    }
    accumulation[index] = float4(color, 1.0f);


    // return float4(dbg.triangleTests / 1000.0f, 0, 0, 1.0f);

    return float4(color, 1.0f);
  }
}
