#include <ren/assets/BVH.h>
#include <ren/core/Instrumentation.h>

#define TINYBVH_IMPLEMENTATION
#include <tinybvh/tiny_bvh.h>

namespace ren {

  // Limits of a leaf reference in the BVH_GPU layout.
  static constexpr u32 kMaxLeafTriangles = 127;
  static constexpr u32 kMaxTriangleEntries = 1u << 24;

  MeshBVH::MeshBVH(std::span<const Vertex> vertices, std::span<const u32> indices) {
    REN_PROFILE_FUNCTION();

    // The SIMD builders need a vertex stride that is a multiple of 16, and
    // they read w. Vertex is 56 bytes, so copy the positions.
    std::vector<tinybvh::bvhvec4> positionData(vertices.size());
    for (size_t i = 0; i < vertices.size(); i++) {
      const glm::vec3 &p = vertices[i].pos;
      positionData[i] = tinybvh::bvhvec4(p.x, p.y, p.z, 0.0f);
    }
    tinybvh::bvhvec4slice positions(positionData.data(), static_cast<u32>(positionData.size()));

    tinybvh::BVH bvh;
    bvh.Build(positions, indices.data(), static_cast<u32>(indices.size() / 3));
    // Threaded builds can leave unused nodes. Compact puts the used nodes
    // first, so the first usedNodes nodes are the full tree.
    bvh.Compact();
    // A leaf reference holds at most 127 triangles. Larger leaves would lose
    // triangles in the conversion.
    bvh.SplitLeafs(kMaxLeafTriangles);
    REN_ASSERT(bvh.idxCount <= kMaxTriangleEntries);

    // gpuBVH refers to bvh, so it must be destroyed first.
    tinybvh::BVH_GPU gpuBVH;
    gpuBVH.ConvertFrom(bvh);

    static_assert(sizeof(tinybvh::BVH_GPU::BVHNode) == sizeof(BVHNode));
    nodes = makeBox<StorageBuffer<BVHNode>>(gpuBVH.usedNodes);
    nodes->copyFromHost(reinterpret_cast<const BVHNode *>(gpuBVH.bvhNode), gpuBVH.usedNodes);

    // The leaves index the triangle list of the source BVH.
    triangles = makeBox<StorageBuffer<u32>>(bvh.idxCount);
    triangles->copyFromHost(bvh.primIdx, bvh.idxCount);
  }

}  // namespace ren
