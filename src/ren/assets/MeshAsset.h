#pragma once

#include <ren/assets/Asset.h>
#include <ren/assets/BVH.h>
#include <ren/assets/Vertex.h>

namespace ren {

  // Final geometry and BVH data of one mesh, before GPU upload.
  class MeshAsset : public Asset {
   public:
    static constexpr AssetType TYPE = AssetType::Mesh;

    std::vector<Vertex> vertices;
    std::vector<u32> indices;
    BakedMeshBVH bvh;

    AssetType getType(void) const override { return TYPE; }

    // Throws if the geometry is not a triangle list with finite vertices, or if the BVH has
    // a reference out of range or a cycle.
    void validate(void) const override;

    void write(Bundle &bundle) const override;

    // Read the geometry and the BVH. Throws if the bundle has a different vertex or node layout.
    static ref<MeshAsset> read(const Bundle &bundle);

   private:
    void validateBVH(void) const;
  };

}  // namespace ren
