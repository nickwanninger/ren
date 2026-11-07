import ren.core;
import ren.surface;
import ren.core.bvh;
import ren.core.raytrace;
import ren.core.random;

// Disable sun traces through the fog while retaining distance fog.
static const bool kVolumetricFog = true;
static const uint kFogSamples = 12;
// Distances use world units.
static const float kFogDensity = 0.012f;
static const float kFogMaxDistance = 120.0f;
static const float3 kFogColor = float3(0.45f, 0.65f, 0.9f);
static const float kAlphaCutoff = 0.1f;
// Bounce and shadow rays start this far from the surface, on the side of the
// incoming ray, so they do not hit the surface again.
static const float kRayOriginOffset = 1e-3f;
// The width of the random offset of each camera ray, as a fraction of a pixel.
// 0 traces the pixel center. 1 covers the full pixel.
static const float kPixelJitter = 0.25f;
static const float kInvPi = 0.318309886f;
// The sun does not move. A moving sun makes the accumulated average incorrect.
static const float3 kSunDirection = float3(0.45f, 0.8660254f, 0.1f) / length(float3(0.45f, 0.8660254f, 0.1f));

[[vk::push_constant]]
cbuffer root {
  TextureHandle skyboxHdri;
  // The running average of the earlier frames. Not valid when
  // accumulatedFrames is 0.
  TextureHandle history;
  Vertex *vertices;
  uint *indices;
  uint numMeshes;
  Mesh *meshes;
  BVHNode *sceneNodes;
  uint *sceneEntries;
  // The emissive meshes, for next event estimation.
  EmissiveLight *lights;
  uint numLights;
  // The cdf of the last light.
  float lightWeightTotal;
  // 1 samples the emissive meshes directly at each diffuse hit. 0 finds them
  // only with bounce rays.
  uint lightSampling;
  uint randomSeed;
  uint maxBounces;
  uint raysPerPixel;
  // Frames already in the running average. 0 starts a new average.
  uint accumulatedFrames;
  // Irradiance from the sun on a surface that faces it. 0 disables the sun.
  float sunStrength;
  uint debugView; // 0 = path traced, 1 = triangle tests, 2 = box tests.
  uint debugTestScale;
}

struct RayTracingMaterial {
  float4 color;
  float4 emissionColor;
  float smoothness;
  // Multiplies color. Not valid when the material has no texture.
  TextureHandle albedoTexture;
  float _pad1;
  float _pad2;
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
  uint opaque;
  // Device addresses of the mesh BVH. These are not pointers because
  // SPIRV-Cross (MoltenVK) emits bad Metal for a copy of a struct that holds
  // pointers.
  uint64_t bvhNodes;
  uint64_t bvhTriangles;
  uint64_t _pad2;
  RayTracingMaterial material;
}

// One emissive mesh. Each triangle of a light has the selection probability
// luminance / lightWeightTotal, so a point on a light has the area density
// luminance / (lightWeightTotal * triangle area).
struct EmissiveLight {
  // The rows of the affine model-to-world matrix.
  float4 modelToWorld[3];
  // The index of the mesh in meshes.
  uint instance;
  uint triangleCount;
  // The sum of the selection weights of this light and the lights before it.
  float cdf;
  float _pad;

  float3 toWorld(float3 position) {
    float4 p = float4(position, 1.0f);
    return float3(dot(modelToWorld[0], p), dot(modelToWorld[1], p), dot(modelToWorld[2], p));
  }
}

float luminance(float3 color) { return dot(color, float3(0.2126f, 0.7152f, 0.0722f)); }

float powerHeuristic(float pdfA, float pdfB) {
  float a = pdfA * pdfA;
  float b = pdfB * pdfB;
  return a + b > 0.0f ? a / (a + b) : 0.0f;
}

// The adapter reads the app's current layout. Materials stay in the app.
struct AppScene : ITraceScene {
  Vertex *vertices;
  uint *indices;
  Mesh *meshes;
  uint numMeshes;
  BVHNode *sceneNodes;
  uint *sceneEntries;

  __init(Vertex *vertices, uint *indices, Mesh *meshes, uint numMeshes, BVHNode *sceneNodes, uint *sceneEntries) {
    this.vertices = vertices;
    this.indices = indices;
    this.meshes = meshes;
    this.numMeshes = numMeshes;
    this.sceneNodes = sceneNodes;
    this.sceneEntries = sceneEntries;
  }

  bool isEmpty() { return numMeshes == 0; }
  MeshBVH getSceneBVH() {
    MeshBVH bvh;
    bvh.nodes = sceneNodes;
    bvh.triangles = sceneEntries;
    return bvh;
  }
  MeshBVH getMeshBVH(uint instance) {
    MeshBVH bvh;
    bvh.nodes = (BVHNode *)meshes[instance].bvhNodes;
    bvh.triangles = (uint *)meshes[instance].bvhTriangles;
    return bvh;
  }
  float4x4 getWorldToObject(uint instance) { return meshes[instance].worldToModel; }
  float3 getMinBounds(uint instance) { return meshes[instance].minBounds; }
  float3 getMaxBounds(uint instance) { return meshes[instance].maxBounds; }
  uint getIndexOffset(uint instance) { return meshes[instance].indexOffset; }
  uint getVertexOffset(uint instance) { return meshes[instance].vertexOffset; }
  uint loadIndex(uint index) { return indices[index]; }
  float3 loadPosition(uint vertex) { return vertices[vertex].position; }
  bool isOpaque(uint instance) { return meshes[instance].opaque != 0; }
}

float4 sampleAlbedo<C : IHitContext>(TriangleAttributes attributes, C context) {
  uint instance = context.instanceIndex();
  var material = meshes[instance].material;
  float4 color = material.color;
  if (material.albedoTexture.isValid()) {
    uint first = meshes[instance].indexOffset + context.primitiveIndex() * 3;
    uint offset = meshes[instance].vertexOffset;
    uint a = offset + indices[first];
    uint b = offset + indices[first + 1];
    uint c = offset + indices[first + 2];
    float2 bary = attributes.barycentrics;
    float2 uv =
        vertices[a].texCoord * (1.0f - bary.x - bary.y) + vertices[b].texCoord * bary.x + vertices[c].texCoord * bary.y;
    color *= material.albedoTexture.SampleLevel(uv, 1.0f);
  }
  return color;
}

AnyHitAction testAlpha<C : IHitContext>(TriangleAttributes attributes, C context) {
  return sampleAlbedo(attributes, context).a < kAlphaCutoff ? AnyHitAction.Ignore : AnyHitAction.Accept;
}

float3 environmentLight(float3 direction) {
  float2 uv =
      float2(0.5f + atan2(direction.x, -direction.z) * (0.5f * kInvPi), acos(clamp(direction.y, -1.0f, 1.0f)) * kInvPi);
  return skyboxHdri.SampleLevel(uv, 2.0f).rgb;
}

// One path vertex. A miss sets only emission, to the environment light.
struct PathPayload {
  bool didHit;
  float3 hitPoint;
  // Faces the incoming ray, for either winding.
  float3 geometricNormal;
  // The interpolated vertex normal, on the same side as geometricNormal.
  float3 shadingNormal;
  float3 albedo;
  float3 emission;
  float smoothness;
  // The solid angle density with which light sampling from the ray origin
  // picks this point. 0 when the surface does not emit.
  float lightPdf;
}

struct PathProgram : ITraceProgram {
  typedef PathPayload Payload;

  // Exercise anyHit even though the app currently marks meshes as opaque.
  override static uint getRayFlags() { return RAY_FLAG_FORCE_NON_OPAQUE; }

  override static AnyHitAction anyHit<C : IHitContext>(inout Payload payload, TriangleAttributes attributes,
                                                       C context) {
    return testAlpha(attributes, context);
  }

  override static void closestHit<C : IHitContext>(inout Payload payload, TriangleAttributes attributes, C context) {
    float3 direction = context.worldRayDirection();
    payload.didHit = true;
    payload.hitPoint = context.worldRayOrigin() + direction * context.rayTCurrent();

    uint instance = context.instanceIndex();
    uint first = meshes[instance].indexOffset + context.primitiveIndex() * 3;
    uint offset = meshes[instance].vertexOffset;
    Vertex a = vertices[offset + indices[first]];
    Vertex b = vertices[offset + indices[first + 1]];
    Vertex c = vertices[offset + indices[first + 2]];

    // Normals move to world space with the transpose of worldToObject.
    float3x3 normalToWorld = transpose((float3x3)context.worldToObject());
    float3 geometric = normalize(mul(normalToWorld, cross(b.position - a.position, c.position - a.position)));
    geometric = dot(geometric, direction) > 0.0f ? -geometric : geometric;
    payload.geometricNormal = geometric;

    float2 bary = attributes.barycentrics;
    float3 interpolated = a.normal * (1.0f - bary.x - bary.y) + b.normal * bary.x + c.normal * bary.y;
    interpolated = mul(normalToWorld, interpolated);
    // A mesh without vertex normals uses the face normal.
    if (dot(interpolated, interpolated) > 0.0f) {
      interpolated = normalize(interpolated);
      payload.shadingNormal = dot(interpolated, geometric) < 0.0f ? -interpolated : interpolated;
    } else {
      payload.shadingNormal = geometric;
    }

    var material = meshes[instance].material;
    payload.albedo = saturate(sampleAlbedo(attributes, context).rgb);
    payload.emission = material.emissionColor.rgb * material.emissionColor.a;
    payload.smoothness = material.smoothness;

    payload.lightPdf = 0.0f;
    float emitted = luminance(payload.emission);
    if (emitted > 0.0f && numLights > 0) {
      float4x4 objectToWorld = context.objectToWorld();
      float3 wa = mul(objectToWorld, float4(a.position, 1.0f)).xyz;
      float3 wb = mul(objectToWorld, float4(b.position, 1.0f)).xyz;
      float3 wc = mul(objectToWorld, float4(c.position, 1.0f)).xyz;
      float area = 0.5f * length(cross(wb - wa, wc - wa));
      // The ray direction has unit length, so t is the distance.
      float distance = context.rayTCurrent();
      float cosLight = abs(dot(geometric, direction));
      if (area > 0.0f && cosLight > 0.0f) {
        payload.lightPdf = emitted / (lightWeightTotal * area) * distance * distance / cosLight;
      }
    }
  }

  override static void miss<C : IRayContext>(inout Payload payload, C context) {
    payload.didHit = false;
    payload.emission = environmentLight(context.worldRayDirection());
  }
}

struct OcclusionPayload {
  bool occluded;
}

struct SunOcclusionProgram : ITraceProgram {
  typedef OcclusionPayload Payload;

  override static uint getRayFlags() {
    return RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER;
  }

  // override static AnyHitAction anyHit<C : IHitContext>(inout Payload payload, TriangleAttributes attributes, C
  // context) {
  //   return testAlpha(attributes, context);
  // }

  override static void miss<C : IRayContext>(inout Payload payload, C context) { payload.occluded = false; }
}

// 1 when nothing blocks the ray before tMax, else 0.
float visibility(float3 position, float3 direction, float tMax,
                 inout Tracer<SunOcclusionProgram, AppScene, true> tracer) {
  Ray ray;
  ray.update(position, direction);
  // The surface caller offsets the origin. Keep nearby wall intersections.
  ray.tMin = 0.0f;
  ray.tMax = tMax;
  OcclusionPayload payload;
  payload.occluded = true;
  tracer.trace(ray, payload);
  return payload.occluded ? 0.0f : 1.0f;
}

// The first light whose cdf is more than weight.
uint findLight(float weight) {
  uint low = 0;
  uint high = numLights - 1;
  while (low < high) {
    uint middle = (low + high) / 2;
    if (lights[middle].cdf <= weight) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }
  return low;
}

// Next event estimation for a Lambertian surface: pick a point on an emissive
// triangle and trace a shadow ray to it. The result has the MIS weight
// against cosine sampling, and does not include the albedo.
float3 sampleEmissiveLights(float3 origin, float3 shadingNormal, float3 geometricNormal, inout uint rngState,
                            inout Tracer<SunOcclusionProgram, AppScene, true> tracer) {
  EmissiveLight light = lights[findLight(RandomValue(rngState) * lightWeightTotal)];
  uint triangle = min(uint(RandomValue(rngState) * light.triangleCount), light.triangleCount - 1);
  uint first = meshes[light.instance].indexOffset + triangle * 3;
  uint offset = meshes[light.instance].vertexOffset;
  float3 a = light.toWorld(vertices[offset + indices[first]].position);
  float3 b = light.toWorld(vertices[offset + indices[first + 1]].position);
  float3 c = light.toWorld(vertices[offset + indices[first + 2]].position);

  // A uniform point on the triangle.
  float rootU = sqrt(RandomValue(rngState));
  float v = RandomValue(rngState);
  float3 target = a * (1.0f - rootU) + b * (rootU * (1.0f - v)) + c * (rootU * v);

  float3 face = cross(b - a, c - a);
  float area = 0.5f * length(face);
  float3 toLight = target - origin;
  float distanceSquared = dot(toLight, toLight);
  if (area <= 0.0f || distanceSquared <= 0.0f) {
    return 0.0f;
  }
  float distance = sqrt(distanceSquared);
  float3 direction = toLight / distance;

  float cosSurface = dot(shadingNormal, direction);
  float cosLight = abs(dot(face, direction)) / (2.0f * area);
  if (cosSurface <= 0.0f || dot(geometricNormal, direction) <= 0.0f || cosLight <= 0.0f) {
    return 0.0f;
  }

  // Stop short of the light, so that its own triangle does not block the ray.
  if (visibility(origin, direction, distance * (1.0f - 1e-3f), tracer) == 0.0f) {
    return 0.0f;
  }

  float3 emission = meshes[light.instance].material.emissionColor.rgb * meshes[light.instance].material.emissionColor.a;
  float lightPdf = luminance(emission) / (lightWeightTotal * area) * distanceSquared / cosLight;
  float bsdfPdf = cosSurface * kInvPi;
  // Lambertian: f * cos / pdf = (albedo / pi) * cos / lightPdf.
  return emission * (cosSurface * kInvPi / lightPdf) * powerHeuristic(lightPdf, bsdfPdf);
}

// Follow one path from the camera, and return the light that it carries back.
// Bounce 0 is the primary ray. Diffuse surfaces sample the sun and the
// emissive meshes directly (next event estimation). A bounce ray cannot hit
// the sun, which is a delta light.
float3 tracePath(inout Ray ray, AppScene scene, inout uint rngState, inout TraceStats stats) {
  var tracer = Tracer<PathProgram, AppScene, true>(scene);
  var occlusionTracer = Tracer<SunOcclusionProgram, AppScene, true>(scene);
  float3 radiance = 0.0f;
  float3 throughput = 1.0f;

  // The last vertex sampled the emissive meshes, so emission that the bounce
  // ray finds gets the MIS weight against light sampling.
  bool lastSampledLights = false;
  float lastBsdfPdf = 0.0f;

  for (uint bounce = 0; bounce <= maxBounces; bounce++) {
    PathPayload hit = {};
    tracer.trace(ray, hit);
    float emissionWeight = lastSampledLights && hit.lightPdf > 0.0f ? powerHeuristic(lastBsdfPdf, hit.lightPdf) : 1.0f;
    radiance += hit.emission * throughput * emissionWeight;
    if (!hit.didHit) {
      break;
    }

    float3 origin = hit.hitPoint + hit.geometricNormal * kRayOriginOffset;
    float3 direction;
    if (RandomValue(rngState) < hit.smoothness) {
      // Mirror bounce. A delta lobe cannot use next event estimation.
      direction = reflect(ray.dir, hit.shadingNormal);
      lastSampledLights = false;
    } else {
      // Lambertian: f * cos = (albedo / pi) * cos.
      float cosSun = dot(hit.shadingNormal, kSunDirection);
      if (sunStrength > 0.0f && cosSun > 0.0f && dot(hit.geometricNormal, kSunDirection) > 0.0f) {
        float sunVisibility = visibility(origin, kSunDirection, kBVHMiss, occlusionTracer);
        radiance += throughput * hit.albedo * (kInvPi * cosSun * sunStrength * sunVisibility);
      }

      lastSampledLights = lightSampling != 0 && numLights > 0;
      if (lastSampledLights) {
        radiance += throughput * hit.albedo *
                    sampleEmissiveLights(origin, hit.shadingNormal, hit.geometricNormal, rngState, occlusionTracer);
      }

      // Cosine weighted random direction. The cosine and the pdf cancel.
      direction = normalize(hit.shadingNormal + RandomDirection(rngState));
      lastBsdfPdf = max(dot(hit.shadingNormal, direction), 0.0f) * kInvPi;
    }
    throughput *= hit.albedo;

    ray.update(origin, direction);
    ray.tMin = 0.0f;
    ray.tMax = kBVHMiss;
  }

  stats += tracer.getStats();
  stats += occlusionTracer.getStats();
  return radiance;
}

export struct Surface : IFullscreenSurface = RaytraceFullScreenSurface;
struct RaytraceFullScreenSurface : IFullscreenSurface {
  static float4 shade(FullscreenVarying input) {
    // The fullscreen triangle runs each pixel once, so each fragment owns one
    // pixel of the history.
    uint2 size = uint2(frameGlobals.renderSize);
    uint2 pixel = min(uint2(input.uv * frameGlobals.renderSize), size - 1);
    uint rngState = (pixel.y * size.x + pixel.x) + randomSeed * 719393u;

    // The view matrix maps world to view space: v = R * p + t. The camera is
    // the view-space origin, so cameraPos = -transpose(R) * t.
    float3x3 viewRotation = (float3x3)frameGlobals.viewMatrix;
    float3 translation =
        float3(frameGlobals.viewMatrix[0][3], frameGlobals.viewMatrix[1][3], frameGlobals.viewMatrix[2][3]);
    float3 cameraPosition = -mul(transpose(viewRotation), translation);

    AppScene scene = AppScene(vertices, indices, meshes, numMeshes, sceneNodes, sceneEntries);
    TraceStats stats = {};
    uint samples = max(raysPerPixel, 1u);
    float3 color = 0.0f;
    for (uint i = 0; i < samples; i++) {
      // A random point in the pixel, for antialiasing.
      float2 jitter = (float2(RandomValue(rngState), RandomValue(rngState)) - 0.5f) * kPixelJitter;
      float2 uv = (float2(pixel) + 0.5f + jitter) / float2(size);
      float2 ndc = uv * 2.0f - 1.0f;
      float3 viewRay = float3(ndc.x / frameGlobals.projMatrix[0][0], ndc.y / frameGlobals.projMatrix[1][1], -1.0f);

      Ray ray;
      ray.update(cameraPosition, normalize(mul(transpose(viewRotation), viewRay)));
      ray.tMin = 0.0f;
      ray.tMax = kBVHMiss;
      color += tracePath(ray, scene, rngState, stats);
    }
    color /= float(samples);

    if (debugView != 0) {
      uint count = debugView == 1 ? stats.triangleTests : stats.boundsTests;
      uint scale = max(debugTestScale, 1u);
      return float4(count < scale ? float3(float(count) / scale) : float3(1, 0, 0), 1.0f);
    }

    // Running average: avg += (new - avg) / (n + 1).
    if (accumulatedFrames > 0) {
      float3 average = getTexture2D(history.imageIndex()).Load(int3(pixel, 0)).rgb;
      color = average + (color - average) / float(accumulatedFrames + 1);
    }
    return float4(color, 1.0f);
  }
}
