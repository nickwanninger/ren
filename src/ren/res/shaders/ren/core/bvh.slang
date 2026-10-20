// Bounding volume hierarchy traversal. ren::MeshBVH (src/ren/assets/BVH.h)
// builds the tree on the CPU with tinybvh.

// The distance that BVHNode.intersect returns when the ray misses the box.
public static const float kBVHMiss = 1e30f;

// Distance where the ray enters the box. kBVHMiss when the ray misses the box,
// or enters it at or after tMax. The distance is negative when the ray starts
// inside the box.
public float IntersectBox(float3 boxMin, float3 boxMax, float3 rayPos, float3 invDir, float tMax) {
  float3 t0s = (boxMin - rayPos) * invDir;
  float3 t1s = (boxMax - rayPos) * invDir;
  float3 tsmaller = min(t0s, t1s);
  float3 tbigger = max(t0s, t1s);
  float tmin = max(max(tsmaller.x, tsmaller.y), tsmaller.z);
  float tmax = min(min(tbigger.x, tbigger.y), tbigger.z);
  if (tmax < max(tmin, 0.0f) || tmin >= tMax) {
    return kBVHMiss;
  }
  return tmin;
}

// One node in the 64-byte layout from Aila and Laine. Same layout as
// ren::BVHNode. A node holds the boxes of its two children, so one fetch
// tests both.
//
// left and right are child references. Bit 31 set: the child is a leaf, bits
// 24..30 are its triangle count and bits 0..23 are the index of its first
// entry in the triangle list. Bit 31 clear: the child is the inner node at
// that index.
public struct BVHNode {
  public float3 leftMin;
  public uint left;
  public float3 leftMax;
  public uint right;
  public float3 rightMin;
  uint _pad0;
  public float3 rightMax;
  uint _pad1;

  public static bool isLeaf(uint child) { return (child & 0x80000000u) != 0; }
  public static uint leafTriCount(uint child) { return (child >> 24) & 127u; }
  public static uint leafFirst(uint child) { return child & 0xffffffu; }

  // Distances where the ray enters each child box, from IntersectBox.
  public void intersectChildren(float3 rayPos, float3 invDir, float tMax, out float leftDst, out float rightDst) {
    leftDst = IntersectBox(leftMin, leftMax, rayPos, invDir, tMax);
    rightDst = IntersectBox(rightMin, rightMax, rayPos, invDir, tMax);
  }
}

// Tests one triangle for MeshBVH.traverse. The BVH does not know the vertex
// format, so the caller supplies the triangle test.
public interface IBVHTriangleTest {
  // triangle is the index of a triangle in the mesh index buffer. When the
  // ray hits the triangle before tMax, keep the hit and set tMax to its
  // distance. Return true to stop the traversal, for example when any hit
  // is sufficient. stackTop is the first unused stack entry. A traversal
  // inside the test must use it as its stackBase.
  [mutating]
  public bool test(uint triangle, inout float tMax, uint stackTop);
}

// Number of entries in the traversal stack. A traversal that fills the stack
// skips nodes.
public static const uint kBVHStackSize = 64;

// The traversal stack of the invocation. All traversals share it. A nested
// traversal, for example of a mesh BVH in a leaf of the top-level BVH, uses
// the entries above the entries of the outer traversal. Each entry is a child
// reference and the distance where the ray enters its box.
static uint gBVHStack[kBVHStackSize];
static float gBVHStackDst[kBVHStackSize];

// Same layout as the device addresses from ren::MeshBVH. The root is node 0,
// and it is always an inner node.
public struct MeshBVH {
  public BVHNode *nodes;
  // Each entry is the index of a triangle in the mesh index buffer. A leaf
  // owns a range of entries.
  public uint *triangles;

  // Visits the nodes that the ray enters before tMax, near child first, and
  // calls tester.test for each triangle in each leaf, until tester.test
  // returns true. The direction does not need to be unit length; distances
  // are in units of rayDir. Returns the number of box tests. The traversal
  // uses the stack entries from stackBase up.
  public uint traverse<T : IBVHTriangleTest>(float3 rayPos, float3 rayDir, inout float tMax, inout T tester,
                                             uint stackBase = 0) {
    float3 invDir = 1.0f / rayDir;
    uint boxTests = 0;

    // A hit can make tMax smaller after the push, so a pop skips the entries
    // at or after tMax.
    uint stackPtr = stackBase;
    // A child reference. Node 0 is an inner node.
    uint child = 0;
    while (true) {
      if (BVHNode.isLeaf(child)) {
        uint first = BVHNode.leafFirst(child);
        uint count = BVHNode.leafTriCount(child);
        for (uint i = 0; i < count; i++) {
          if (tester.test(triangles[first + i], tMax, stackPtr)) {
            return boxTests;
          }
        }
      } else {
        BVHNode node = nodes[child];
        float leftDst, rightDst;
        node.intersectChildren(rayPos, invDir, tMax, leftDst, rightDst);
        boxTests += 2;

        uint near = node.left;
        uint far = node.right;
        float nearDst = leftDst;
        float farDst = rightDst;
        if (farDst < nearDst) {
          near = node.right;
          far = node.left;
          nearDst = rightDst;
          farDst = leftDst;
        }

        if (nearDst != kBVHMiss) {
          if (farDst != kBVHMiss && stackPtr < kBVHStackSize) {
            gBVHStack[stackPtr] = far;
            gBVHStackDst[stackPtr] = farDst;
            stackPtr++;
          }
          child = near;
          continue;
        }
      }

      // Pop the next entry that the ray enters before tMax.
      bool found = false;
      while (stackPtr > stackBase) {
        stackPtr--;
        if (gBVHStackDst[stackPtr] < tMax) {
          child = gBVHStack[stackPtr];
          found = true;
          break;
        }
      }
      if (!found) {
        break;
      }
    }
    return boxTests;
  }
}
