import ren.core;
import ren.surface;
import ren.core.bvh;
import ren.core.raytrace;

// Disable sun traces through the fog while retaining distance fog.
static const bool kVolumetricFog = true;
static const uint kFogSamples = 12;
// Distances use world units.
static const float kFogDensity = 0.012f;
static const float kFogMaxDistance = 120.0f;
static const float3 kFogColor = float3(0.45f, 0.65f, 0.9f);
static const float kAlphaCutoff = 0.1f;

[[vk::push_constant]]
cbuffer root {
  TextureHandle skyboxHdri;
  Vertex *vertices;
  uint *indices;
  uint numMeshes;
  Mesh *meshes;
  BVHNode *sceneNodes;
  uint *sceneEntries;
  uint debugView; // 0 = shaded albedo and fog, 1 = triangle tests, 2 = box tests.
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

struct AlbedoPayload {
  float3 color;
  float3 hitPoint;
  bool didHit;
}

struct AlbedoProgram : ITraceProgram {
  typedef AlbedoPayload Payload;

  // Exercise anyHit even though the app currently marks meshes as opaque.
  override static uint getRayFlags() { return RAY_FLAG_FORCE_NON_OPAQUE; }

  override static AnyHitAction anyHit<C : IHitContext>(inout Payload payload, TriangleAttributes attributes, C context) {
    return testAlpha(attributes, context);
  }

  override static void closestHit<C : IHitContext>(inout Payload payload, TriangleAttributes attributes, C context) {
    payload.didHit = true;
    payload.hitPoint = context.worldRayOrigin() + context.worldRayDirection() * context.rayTCurrent();
    payload.color = sampleAlbedo(attributes, context).rgb;
  }

  override static void miss<C : IRayContext>(inout Payload payload, C context) {
    payload.didHit = false;
    float3 direction = context.worldRayDirection();
    static const float invPi = 0.318309886f;
    float2 uv =
        float2(0.5f + atan2(direction.x, -direction.z) * (0.5f * invPi), acos(clamp(direction.y, -1.0f, 1.0f)) * invPi);
    payload.color = skyboxHdri.SampleLevel(uv, 2.0f).rgb;
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

  // override static AnyHitAction anyHit<C : IHitContext>(inout Payload payload, TriangleAttributes attributes, C context) {
  //   return testAlpha(attributes, context);
  // }

  override static void miss<C : IRayContext>(inout Payload payload, C context) { payload.occluded = false; }
}

float sunVisibility(float3 position, float3 direction, inout Tracer<SunOcclusionProgram, AppScene, true> tracer) {
  Ray ray;
  ray.update(position, direction);
  ray.tMin = 1e-3f;
  ray.tMax = kBVHMiss;
  OcclusionPayload payload;
  payload.occluded = true;
  tracer.trace(ray, payload);
  return payload.occluded ? 0.0f : 1.0f;
}

export struct Surface : IFullscreenSurface = RaytraceFullScreenSurface;
struct RaytraceFullScreenSurface : IFullscreenSurface {
  static float4 shade(FullscreenVarying input) {
    float2 ndc = input.uv * 2.0f - 1.0f;
    float3 viewRay = float3(ndc.x / frameGlobals.projMatrix[0][0], ndc.y / frameGlobals.projMatrix[1][1], -1.0f);
    float3x3 viewRotation = (float3x3)frameGlobals.viewMatrix;
    float3 direction = normalize(mul(transpose(viewRotation), viewRay));
    float3 translation =
        float3(frameGlobals.viewMatrix[0][3], frameGlobals.viewMatrix[1][3], frameGlobals.viewMatrix[2][3]);

    Ray ray;
    ray.update(-mul(transpose(viewRotation), translation), direction);
    ray.tMin = 0.0f;
    ray.tMax = kBVHMiss;

    AppScene scene = AppScene(vertices, indices, meshes, numMeshes, sceneNodes, sceneEntries);
    Tracer<AlbedoProgram, AppScene, true> tracer = Tracer<AlbedoProgram, AppScene, true>(scene);
    AlbedoPayload payload = {};
    tracer.trace(ray, payload);
    var color = payload.color;
    var stats = tracer.getStats();

    // Rotate around Y once every 45 seconds. Keep the current elevation.
    static const float3 sunBaseDirection = float3(0.45f, 0.8660254f, 0.1f);
    float sunAngle = frac(frameGlobals.time / 45.0f) * 6.2831853f;
    float sunSin, sunCos;
    sincos(sunAngle, sunSin, sunCos);
    float3 sunDirection =
        normalize(float3(sunBaseDirection.x * sunCos + sunBaseDirection.z * sunSin, sunBaseDirection.y,
                         -sunBaseDirection.x * sunSin + sunBaseDirection.z * sunCos));
    var sunTracer = Tracer<SunOcclusionProgram, AppScene, true>(scene);
    if (payload.didHit) {
      // Keep a small ambient term so blocked surfaces retain their albedo.
      color *= lerp(0.15f, 1.0f, sunVisibility(payload.hitPoint, sunDirection, sunTracer));
    }
    stats += sunTracer.getStats();

    if (debugView != 0) {
      uint count = debugView == 1 ? stats.triangleTests : stats.boundsTests;
      uint scale = max(debugTestScale, 1u);
      color = count < scale ? float3(float(count) / scale) : float3(1, 0, 0);
    }
    return float4(color, 1.0f);
  }
}
