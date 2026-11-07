#include <ren/assets/Mesh.h>
#include <ren/assets/MegaMeshBuffer.h>
#include <ren/core/Application.h>
#include <ren/core/Flag.h>

#include <meshoptimizer/meshoptimizer.h>

static ren::Flag<float> meshLODRatio("mesh-lod-ratio", 1.0f,
                                     "Fraction of triangles to keep when a mesh loads, 1 = no simplification");
static ren::Flag<float> meshLODError("mesh-lod-error", 0.01f,
                                     "Maximum simplification error, relative to the mesh extents");


namespace ren {

  // Makes a LOD of the mesh with meshopt_simplify. The LOD uses the same
  // vertices. Returns indices unchanged when the LOD flag is off.
  static std::vector<u32> simplify(const std::string &name, const std::vector<Vertex> &vertices,
                                   const std::vector<u32> &indices) {
    float ratio = meshLODRatio.get();
    if (ratio >= 1.0f || indices.empty()) {
      return indices;
    }
    REN_PROFILE_FUNCTION();

    size_t targetCount = static_cast<size_t>(static_cast<double>(indices.size() / 3) * ratio) * 3;
    std::vector<u32> lod(indices.size());
    float resultError = 0.0f;
    size_t count = meshopt_simplify(lod.data(), indices.data(), indices.size(), &vertices[0].pos.x, vertices.size(),
                                    sizeof(Vertex), targetCount, meshLODError.get(), 0, &resultError);
    lod.resize(count);
    fmt::print("Mesh LOD '{}': {} -> {} triangles, error {:.4f}\n", name, indices.size() / 3, count / 3,
               resultError);
    return lod;
  }


  Mesh::Mesh(const std::string &name, const std::vector<Vertex> &vertices,
             const std::vector<u32> &sourceIndices)
      : name(name)
      , vertexCount(vertices.size()) {
    REN_PROFILE_FUNCTION();

    // The rasterizer and the BVH must use the same index buffer, because the
    // BVH triangle entries index it.
    std::vector<u32> indices = simplify(name, vertices, sourceIndices);
    indexCount = indices.size();

    for (auto &vert : vertices) {
      aabb.update(vert.pos);
    }

    this->megaHandle = ren::resource<ren::MegaMeshBuffer>().allocate(vertices, indices);

    // TODO: build only for meshes that need ray tracing.
    if (!indices.empty()) {
      this->bvh = makeBox<MeshBVH>(vertices, indices);
    }
  }

  Mesh::~Mesh() {}


}  // namespace ren