#include <ren/assets/BVH.h>
#include <ren/assets/Mesh.h>
#include <ren/core/Instrumentation.h>

// Use TinyBVH's scalar path when the x86 build has no SIMD extensions enabled.
#if (defined(__x86_64__) || defined(_M_X64)) && !defined(__SSE4_2__) && !defined(__AVX__) && !defined(_MSC_VER)
#define TINYBVH_NO_SIMD
#endif
#define TINYBVH_IMPLEMENTATION
#include <tinybvh/tiny_bvh.h>

#include <chrono>
#include <fmt/core.h>

namespace ren {

  // Limits of a leaf reference in the BVH_GPU layout.
  static constexpr u32 kMaxLeafTriangles = 127;
  static constexpr u32 kMaxTriangleEntries = 1u << 24;

  // Number of nodes on the longest path from the root to a leaf.
  static u32 maxDepth(const tinybvh::BVH &bvh) {
    struct Entry {
      u32 node;
      u32 depth;
    };
    std::vector<Entry> stack = {{0, 1}};
    u32 result = 0;
    while (!stack.empty()) {
      Entry entry = stack.back();
      stack.pop_back();
      const auto &node = bvh.bvhNode[entry.node];
      result = std::max(result, entry.depth);
      if (!node.isLeaf()) {
        stack.push_back({node.leftFirst, entry.depth + 1});
        stack.push_back({node.leftFirst + 1, entry.depth + 1});
      }
    }
    return result;
  }

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

    auto start = std::chrono::steady_clock::now();
    tinybvh::BVH bvh;
    // SBVH build. Spatial splits put one triangle in more than one leaf when
    // that makes the child boxes overlap less.
    bvh.BuildHQ(positions, indices.data(), static_cast<u32>(indices.size() / 3));
    // Threaded builds can leave unused nodes. Compact puts the used nodes
    // first, so the first usedNodes nodes are the full tree. Optimize also
    // requires a tree with no unused nodes.
    bvh.Compact();
    // Moves subtrees to lower the SAH cost.
    bvh.Optimize();
    // A leaf reference holds at most 127 triangles. Larger leaves would lose
    // triangles in the conversion.
    bvh.SplitLeafs(kMaxLeafTriangles);
    REN_ASSERT(bvh.idxCount <= kMaxTriangleEntries);

    auto buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    fmt::print("MeshBVH: {} triangles, {} entries, {} nodes, depth {}, SAH cost {:.2f}, {:.1f} ms\n",
               indices.size() / 3, bvh.idxCount, bvh.usedNodes, maxDepth(bvh), bvh.SAHCost(), buildMs);

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

  BoundsBVH buildBoundsBVH(std::span<const AABB> boxes) {
    REN_PROFILE_FUNCTION();
    REN_ASSERT(!boxes.empty());

    tinybvh::BVH bvh;
    bvh.customUserdata = const_cast<AABB *>(boxes.data());
    bvh.Build(
        [](const u32 i, tinybvh::bvhvec3 &boxMin, tinybvh::bvhvec3 &boxMax, void *userdata) {
          const AABB &box = static_cast<const AABB *>(userdata)[i];
          boxMin = tinybvh::bvhvec3(box.min.x, box.min.y, box.min.z);
          boxMax = tinybvh::bvhvec3(box.max.x, box.max.y, box.max.z);
        },
        static_cast<u32>(boxes.size()));
    bvh.Compact();
    bvh.SplitLeafs(kMaxLeafTriangles);

    // gpuBVH refers to bvh, so it must be destroyed first.
    tinybvh::BVH_GPU gpuBVH;
    gpuBVH.ConvertFrom(bvh);

    BoundsBVH result;
    const auto *nodes = reinterpret_cast<const BVHNode *>(gpuBVH.bvhNode);
    result.nodes.assign(nodes, nodes + gpuBVH.usedNodes);
    result.entries.assign(bvh.primIdx, bvh.primIdx + bvh.idxCount);
    return result;
  }

}  // namespace ren
