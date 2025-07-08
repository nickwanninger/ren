#pragma once

#include <ren/types.h>
#include <ren/renderer/Buffer.h>
#include <ren/assets/Vertex.h>
#include <ren/core/UUID.h>

namespace ren {

  class Mesh : public ren::HasUUID {
   public:
    Mesh(const std::string &name, const std::vector<Vertex> &vertices,
         const std::vector<u32> &indices);

    ~Mesh();

    // Get the name of the mesh.
    const std::string &getName(void) const { return name; }

    // Get the vertex buffer.
    ref<VertexBuffer<ren::Vertex>> getVertexBuffer(void) const { return vertexBuffer; }

    // Get the index buffer.
    ref<IndexBuffer> getIndexBuffer(void) const { return indexBuffer; }

    // Get the number of indices.
    u32 getIndexCount(void) const { return static_cast<u32>(indices.size()); }
    u32 getVertexCount(void) const { return static_cast<u32>(vertices.size()); }

   private:
    std::string name;
    ref<VertexBuffer<ren::Vertex>> vertexBuffer;
    ref<IndexBuffer> indexBuffer;
    std::vector<Vertex> vertices;
    std::vector<u32> indices;
  };

  using MeshRef = ref<Mesh>;

}  // namespace ren