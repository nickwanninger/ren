import ren.core.bvh;

// Software tracing uses the same flag values as Slang's hardware interface.
// Callbacks return before the consumer issues another trace.
public struct Ray {
  public float3 pos;
  public float3 dir;
  public float3 invDir;
  public float tMin = 0.0f;
  public float tMax = kBVHMiss;

  [mutating]
  public void update(float3 pos, float3 d) {
    this.pos = pos;
    this.dir = d;
    this.invDir = 1.0f / d;
  }
}

// Same data as BuiltInTriangleIntersectionAttributes. That built-in type
// requires spvRayTracingKHR in Slang 2026.19, even for software traversal.
public struct TriangleAttributes {
  public float2 barycentrics;
}

public enum AnyHitAction {
  Ignore,
  Accept,
  AcceptAndEndSearch,
}

// One mesh geometry per instance. The adapter owns no GPU resources. It
// supplies references to data that the consumer prepares before dispatch.
// The transforms must be invertible affine transforms. Mesh bounds and BVHs
// use object space. Triangle IDs are local to the mesh index range.
public interface ITraceScene {
  bool isEmpty();
  MeshBVH getSceneBVH();
  MeshBVH getMeshBVH(uint instance);
  float4x4 getWorldToObject(uint instance);
  float3 getMinBounds(uint instance);
  float3 getMaxBounds(uint instance);
  uint getIndexOffset(uint instance);
  uint getVertexOffset(uint instance);
  uint loadIndex(uint index);
  float3 loadPosition(uint vertex);
  bool isOpaque(uint instance);
  uint getInstanceID(uint instance) { return instance; }
}

public interface IRayContext {
  float3 worldRayOrigin();
  float3 worldRayDirection();
  float rayTMin();
  float rayTCurrent();
  uint rayFlags();
}

public interface IHitContext : IRayContext {
  uint instanceIndex();
  uint instanceID();
  uint geometryIndex();
  uint primitiveIndex();
  bool frontFace();
  float3 objectRayOrigin();
  float3 objectRayDirection();
  float4x4 worldToObject();
  float4x4 objectToWorld();
}

public struct MissContext : IRayContext {
  Ray ray;
  uint flags;

  public float3 worldRayOrigin() { return ray.pos; }
  public float3 worldRayDirection() { return ray.dir; }
  public float rayTMin() { return ray.tMin; }
  public float rayTCurrent() { return ray.tMax; }
  public uint rayFlags() { return flags; }
}

public struct HitContext<S : ITraceScene> : IHitContext {
  S scene;
  Ray ray;
  float t;
  uint instance;
  uint triangle;
  bool front;
  uint flags;

  public float3 worldRayOrigin() { return ray.pos; }
  public float3 worldRayDirection() { return ray.dir; }
  public float rayTMin() { return ray.tMin; }
  public float rayTCurrent() { return t; }
  public uint rayFlags() { return flags; }
  public uint instanceIndex() { return instance; }
  public uint instanceID() { return scene.getInstanceID(instance); }
  public uint geometryIndex() { return 0; }
  public uint primitiveIndex() { return triangle; }
  public bool frontFace() { return front; }
  public float4x4 worldToObject() { return scene.getWorldToObject(instance); }
  public float3 objectRayOrigin() { return mul(worldToObject(), float4(ray.pos, 1.0f)).xyz; }
  // Keep the direction unnormalized so t stays the same in both spaces.
  public float3 objectRayDirection() { return mul(worldToObject(), float4(ray.dir, 0.0f)).xyz; }

  // Compute this only when a callback needs it. The adapter need not store
  // a second matrix for each instance.
  public float4x4 objectToWorld() {
    float4x4 m = worldToObject();
    float3 a = cross(m[1].xyz, m[2].xyz);
    float3 b = cross(m[2].xyz, m[0].xyz);
    float3 c = cross(m[0].xyz, m[1].xyz);
    float3x3 inverse = transpose(float3x3(a, b, c)) / dot(m[0].xyz, a);
    float3 translation = mul(inverse, -float3(m[0][3], m[1][3], m[2][3]));
    return float4x4(float4(inverse[0], translation.x), float4(inverse[1], translation.y),
                    float4(inverse[2], translation.z), float4(0, 0, 0, 1));
  }
}

public interface ITraceProgram {
  associatedtype Payload;

  // A static function permits a default implementation. Specialization
  // folds a constant return value into the traversal.
  static uint getRayFlags() { return RAY_FLAG_NONE; }

  // Candidate order is unspecified. Rejection preserves payload writes.
  static AnyHitAction anyHit<C : IHitContext>(inout Payload payload, TriangleAttributes attributes, C context) {
    return AnyHitAction.Accept;
  }
  static void closestHit<C : IHitContext>(inout Payload payload, TriangleAttributes attributes, C context) {}
  static void miss<C : IRayContext>(inout Payload payload, C context) {}
}

public struct TraceStats {
  public uint boundsTests;
  public uint triangleTests;

  [mutating]
  public void add(TraceStats other) {
    boundsTests += other.boundsTests;
    triangleTests += other.triangleTests;
  }
}

public TraceStats operator +=(inout TraceStats left, TraceStats right) {
  left.add(right);
  return left;
}

struct TriangleHit {
  float t;
  uint instance;
  uint triangle;
  float2 barycentrics;
  bool front;
}

// Position loads belong to the scene adapter. This test has no dependency on
// the engine vertex layout or material data.
bool intersectTriangle(float3 origin, float3 direction, float3 a, float3 b, float3 c, float tMin, float tMax,
                       out float t, out float2 barycentrics, out bool front) {
  float3 ab = b - a;
  float3 ac = c - a;
  float3 face = cross(ab, ac);
  float determinant = -dot(direction, face);
  float inverseDeterminant = 1.0f / determinant;
  float3 offset = origin - a;
  float3 perpendicular = cross(offset, direction);
  t = dot(offset, face) * inverseDeterminant;
  float u = dot(ac, perpendicular) * inverseDeterminant;
  float v = -dot(ab, perpendicular) * inverseDeterminant;
  barycentrics = float2(u, v);
  front = determinant > 0.0f;
  return abs(determinant) >= 1e-8f && t >= tMin && t < tMax && u >= 0 && v >= 0 && u + v <= 1;
}

HitContext<S> makeHitContext<S : ITraceScene>(S scene, Ray ray, TriangleHit hit, uint flags) {
  HitContext<S> context;
  context.scene = scene;
  context.ray = ray;
  context.t = hit.t;
  context.instance = hit.instance;
  context.triangle = hit.triangle;
  context.front = hit.front;
  context.flags = flags;
  return context;
}

struct MeshTriangleTest<P : ITraceProgram, S : ITraceScene, let collectStats : bool> : IBVHTriangleTest {
  S scene;
  Ray worldRay;
  float3 origin;
  float3 direction;
  uint instance;
  P.Payload payload;
  TriangleHit hit;
  bool didHit;
  bool stopped;
  uint triangleTests;

  [mutating]
  bool test(uint triangle, inout float tMax, uint stackTop) {
    if (collectStats) triangleTests++;
    uint first = scene.getIndexOffset(instance) + triangle * 3;
    uint offset = scene.getVertexOffset(instance);
    float3 a = scene.loadPosition(offset + scene.loadIndex(first));
    float3 b = scene.loadPosition(offset + scene.loadIndex(first + 1));
    float3 c = scene.loadPosition(offset + scene.loadIndex(first + 2));
    TriangleHit candidate;
    if (!intersectTriangle(origin, direction, a, b, c, worldRay.tMin, tMax,
                           candidate.t, candidate.barycentrics, candidate.front)) return false;

    uint flags = P.getRayFlags();
    if (((flags & RAY_FLAG_CULL_BACK_FACING_TRIANGLES) != 0 && !candidate.front) ||
        ((flags & RAY_FLAG_CULL_FRONT_FACING_TRIANGLES) != 0 && candidate.front)) return false;

    candidate.instance = instance;
    candidate.triangle = triangle;
    bool opaque = (flags & RAY_FLAG_FORCE_OPAQUE) != 0 ||
                  ((flags & RAY_FLAG_FORCE_NON_OPAQUE) == 0 && scene.isOpaque(instance));
    AnyHitAction action = AnyHitAction.Accept;
    if (!opaque) {
      TriangleAttributes attributes;
      attributes.barycentrics = candidate.barycentrics;
      action = P.anyHit(payload, attributes, makeHitContext(scene, worldRay, candidate, flags));
    }
    if (action == AnyHitAction.Ignore) return false;

    // Rejected candidates must not shorten the search interval.
    tMax = candidate.t;
    hit = candidate;
    didHit = true;
    stopped = action == AnyHitAction.AcceptAndEndSearch || (flags & RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH) != 0;
    return stopped;
  }
}

struct SceneInstanceTest<P : ITraceProgram, S : ITraceScene, let collectStats : bool> : IBVHTriangleTest {
  S scene;
  Ray ray;
  P.Payload payload;
  TriangleHit hit;
  bool didHit;
  TraceStats stats;

  [mutating]
  bool test(uint instance, inout float tMax, uint stackTop) {
    float4x4 transform = scene.getWorldToObject(instance);
    MeshTriangleTest<P, S, collectStats> tester = {};
    tester.scene = scene;
    tester.worldRay = ray;
    tester.origin = mul(transform, float4(ray.pos, 1.0f)).xyz;
    tester.direction = mul(transform, float4(ray.dir, 0.0f)).xyz;
    tester.instance = instance;
    tester.payload = payload;

    if (collectStats) stats.boundsTests++;
    if (IntersectBox(scene.getMinBounds(instance), scene.getMaxBounds(instance), tester.origin,
                     1.0f / tester.direction, tMax) == kBVHMiss) return false;

    MeshBVH bvh = scene.getMeshBVH(instance);
    uint boxTests = bvh.traverse(tester.origin, tester.direction, tMax, tester, stackTop);
    if (collectStats) {
      stats.boundsTests += boxTests;
      stats.triangleTests += tester.triangleTests;
    }
    payload = tester.payload;
    if (tester.didHit) {
      hit = tester.hit;
      didHit = true;
    }
    return tester.stopped;
  }
}

// Each instance owns its scene adapter and optional counters. Traversal uses
// the existing 64-entry invocation stack. Do not trace from a callback.
public struct Tracer<P : ITraceProgram, S : ITraceScene, let collectStats : bool = false> {
  S scene;
  TraceStats stats;

  public __init(S scene) {
    this.scene = scene;
    this.stats = {};
  }

  public TraceStats getStats() { return stats; }

  [mutating]
  public void resetStats() { stats = {}; }

  // The consumer initializes the payload. Every trace calls closestHit or
  // miss, except that SKIP_CLOSEST_HIT_SHADER suppresses closestHit on a hit.
  [mutating]
  public void trace(Ray ray, inout P.Payload payload) {
    SceneInstanceTest<P, S, collectStats> tester = {};
    tester.scene = scene;
    tester.ray = ray;
    tester.payload = payload;

    if (!scene.isEmpty() && ray.tMin < ray.tMax) {
      MeshBVH bvh = scene.getSceneBVH();
      float tMax = ray.tMax;
      uint boxTests = bvh.traverse(ray.pos, ray.dir, tMax, tester);
      if (collectStats) {
        stats.boundsTests += boxTests;
        stats += tester.stats;
      }
    }
    payload = tester.payload;

    uint flags = P.getRayFlags();
    if (tester.didHit) {
      if ((flags & RAY_FLAG_SKIP_CLOSEST_HIT_SHADER) == 0) {
        TriangleAttributes attributes;
        attributes.barycentrics = tester.hit.barycentrics;
        P.closestHit(payload, attributes, makeHitContext(scene, ray, tester.hit, flags));
      }
    } else {
      MissContext context;
      context.ray = ray;
      context.flags = flags;
      P.miss(payload, context);
    }
  }
}
