#pragma once

#include <ren/types.h>
#include <ren/renderer/Buffer.h>
#include <ren/assets/Vertex.h>

namespace ren {

  struct AABB;

  // One BVH node in the 64-byte layout from Aila and Laine (tinybvh BVH_GPU).
  // A node holds the boxes of its two children, so one fetch tests both. The
  // shaders use the same layout.
  //
  // left and right are child references. Bit 31 set: the child is a leaf,
  // bits 24..30 are its triangle count and bits 0..23 are the index of its
  // first entry in the triangle list. Bit 31 clear: the child is the inner
  // node at that index.
  struct BVHNode {
    glm::vec3 leftMin;
    u32 left;
    glm::vec3 leftMax;
    u32 right;
    glm::vec3 rightMin;
    u32 _pad0;
    glm::vec3 rightMax;
    u32 _pad1;
  };
  static_assert(sizeof(BVHNode) == 64);


  // Bounding volume hierarchy over the triangles of one mesh, in model space.
  // The root is node 0, and it is always an inner node. The GPU reads the
  // nodes and the triangle list through buffer device addresses.
  class MeshBVH {
   public:
    MeshBVH(std::span<const Vertex> vertices, std::span<const u32> indices);

    uintptr_t getNodeAddress(void) const { return nodes->getGPUAddress(); }
    uintptr_t getTriangleAddress(void) const { return triangles->getGPUAddress(); }
    u32 getNodeCount(void) const { return static_cast<u32>(nodes->count()); }

   private:
    Box<StorageBuffer<BVHNode>> nodes;
    // Each entry is the index of a triangle in the mesh index buffer (the
    // triangle uses indices 3t, 3t + 1 and 3t + 2). A leaf owns a range of
    // entries.
    Box<StorageBuffer<u32>> triangles;
  };


  // A BVH over boxes, in the same layout as MeshBVH. Each leaf entry is the
  // index of a box. The root is node 0, and it is always an inner node.
  struct BoundsBVH {
    std::vector<BVHNode> nodes;
    std::vector<u32> entries;
  };

  // Builds on the CPU. boxes must not be empty.
  BoundsBVH buildBoundsBVH(std::span<const AABB> boxes);

}  // namespace ren
