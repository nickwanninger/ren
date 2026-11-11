#include <ren/assets/MeshAsset.h>
#include <ren/core/Bundle.h>

#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace ren {

  //=========================
  // Validation
  //=========================

  void MeshAsset::validate(void) const {
    if (vertices.empty() || indices.empty() || indices.size() % 3 != 0 || vertices.size() > std::numeric_limits<u32>::max() ||
        indices.size() > std::numeric_limits<u32>::max()) {
      throw std::runtime_error("Invalid mesh geometry");
    }

    for (u32 index : indices) {
      if (index >= vertices.size()) {
        throw std::runtime_error("Mesh vertex index is out of range");
      }
    }

    for (const auto &vertex : vertices) {
      std::array<float, sizeof(Vertex) / sizeof(float)> values;
      std::memcpy(values.data(), &vertex, sizeof(vertex));
      for (float value : values) {
        if (!std::isfinite(value)) {
          throw std::runtime_error("Mesh vertex contains a non-finite value");
        }
      }
    }

    validateBVH();
  }


  void MeshAsset::validateBVH(void) const {
    if (bvh.nodes.empty() || bvh.triangles.empty()) {
      throw std::runtime_error("Mesh bundle has no baked BVH");
    }

    for (u32 triangle : bvh.triangles) {
      if (triangle >= indices.size() / 3) {
        throw std::runtime_error("BVH triangle index is out of range");
      }
    }

    // A traversal rejects cycles as well as references outside the stored arrays.
    // State 1 marks a node on the current path, and state 2 marks a finished node.
    std::vector<u8> state(bvh.nodes.size(), 0);
    std::vector<std::pair<u32, bool>> stack = {{0, false}};
    while (!stack.empty()) {
      auto [index, leaving] = stack.back();
      stack.pop_back();
      if (leaving) {
        state[index] = 2;
        continue;
      }
      if (state[index] == 1) {
        throw std::runtime_error("BVH contains a cycle");
      }
      if (state[index] == 2) {
        continue;
      }

      state[index] = 1;
      stack.push_back({index, true});
      const auto &node = bvh.nodes[index];
      for (u32 child : {node.left, node.right}) {
        if (child & 0x80000000u) {
          u32 count = (child >> 24) & 127u;
          u32 first = child & 0x00ffffffu;
          if (first > bvh.triangles.size() || count > bvh.triangles.size() - first) {
            throw std::runtime_error("BVH leaf range is out of bounds");
          }
        } else {
          if (child >= bvh.nodes.size()) {
            throw std::runtime_error("BVH node index is out of range");
          }
          stack.push_back({child, false});
        }
      }
    }
  }



  //=========================
  // Bundle
  //=========================

  void MeshAsset::write(Bundle &bundle) const {
    bundle.setKey("vertexStride", sizeof(Vertex));
    bundle.setKey("bvhNodeStride", sizeof(BVHNode));
    bundle.attachBlob("vertices", vertices);
    bundle.attachBlob("indices", indices);
    bundle.attachBlob("bvhNodes", bvh.nodes);
    bundle.attachBlob("bvhTriangles", bvh.triangles);
  }


  ref<MeshAsset> MeshAsset::read(const Bundle &bundle) {
    const auto &meta = bundle.getMetadata();
    if (meta.at("vertexStride") != sizeof(Vertex) || meta.at("bvhNodeStride") != sizeof(BVHNode)) {
      throw std::runtime_error("Unsupported mesh layout");
    }

    auto mesh = make<MeshAsset>();
    mesh->vertices = bundle.readBlob<Vertex>("vertices");
    mesh->indices = bundle.readBlob<u32>("indices");
    mesh->bvh.nodes = bundle.readBlob<BVHNode>("bvhNodes");
    mesh->bvh.triangles = bundle.readBlob<u32>("bvhTriangles");
    return mesh;
  }

}  // namespace ren
