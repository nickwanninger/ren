#pragma once

#include <ren/types.h>
#include <ren/renderer/Buffer.h>
#include <ren/assets/Vertex.h>
#include <ren/core/UUID.h>
#include <ren/assets/MegaMeshBuffer.h>

namespace ren {

  struct AABB {
    glm::vec3 min = glm::vec3(0.0f);
    glm::vec3 max = glm::vec3(0.0f);

    AABB() = default;

    AABB(const glm::vec3 &min, const glm::vec3 &max)
        : min(min)
        , max(max) {}

    bool isValid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }

    void update(const glm::vec3 &point) {
      if (!isValid()) {
        min = point;
        max = point;
      } else {
        min = glm::min(min, point);
        max = glm::max(max, point);
      }
    }
  };

  class Mesh : public ren::HasUUID {
   public:
    Mesh(const std::string &name, const std::vector<Vertex> &vertices,
         const std::vector<u32> &indices);

    ~Mesh();

    // Get the name of the mesh.
    const std::string &getName(void) const { return name; }

    // Get the vertex buffer.
    // ref<VertexBuffer<ren::Vertex>> getVertexBuffer(void) { return vertexBuffer; }

    // Get the index buffer.
    // ref<IndexBuffer> getIndexBuffer(void) { return indexBuffer; }

    // Get the number of indices.
    u32 getIndexCount(void) const { return indexCount; }
    u32 getVertexCount(void) const { return vertexCount; }
    const AABB &getAABB(void) const { return aabb; }


    // TEMP:
    ren::MegaMeshHandle megaHandle = 0;

   private:
    std::string name;
    // ref<VertexBuffer<ren::Vertex>> vertexBuffer;
    // ref<IndexBuffer> indexBuffer;
    u32 vertexCount = 0;
    u32 indexCount = 0;
    AABB aabb;  // Axis-aligned bounding box for the mesh.
  };

  using MeshRef = ref<Mesh>;

}  // namespace ren