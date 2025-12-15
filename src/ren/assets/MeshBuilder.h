#pragma once

#include <ren/types.h>
#include <ren/assets/Vertex.h>
#include <variant>

namespace ren {


  struct MeshData {
    std::vector<ren::Vertex> vertices;
    std::vector<u32> indices;


    // debugging utility to dump the mesh as an obj to stdout.
    void dumpObj(void);
  };


  class FaceBuilder;

  // Either a vertex index or a full vertex.
  using PendingVertex = std::variant<u32, ren::Vertex>;

  /**
   * This class is a builder for creating meshes procedurally, and
   * provides an interface to add vertices, indices, and other attributes.
   *
   *
   */
  class MeshBuilder {
   public:
    MeshBuilder(void);


    // Add a vertex to the mesh, returning its index.
    template <typename... Args>
    inline u32 vertex(Args &&...args) {
      ren::Vertex v(std::forward<Args>(args)...);
      data->vertices.push_back(v);
      return static_cast<u32>(data->vertices.size() - 1);
    }

    // Update an existing vertex
    void vertex(u32 idx, const ren::Vertex &v);

    // Add an index to the mesh.
    void index(u32 idx);


    // Set the UV coordinates of a given vertex.
    inline void setUV(u32 vertexIndex, glm::vec2 uv) { data->vertices[vertexIndex].texCoord = uv; }

    // Return a copy of the mesh data, now owned by the caller.
    ref<MeshData> stampOut(void);


    const ren::Vertex &getVertex(u32 index) const { return data->vertices[index]; }



    FaceBuilder beginFace(void);

   protected:
    friend class MeshSubBuilder;
    ref<MeshData> data;
  };


  class MeshSubBuilder {
   protected:
    friend class MeshBuilder;
    explicit MeshSubBuilder(MeshBuilder &builder)
        : builder(builder) {}
    MeshBuilder builder;
  };


  class FaceBuilder : public MeshSubBuilder {
   public:
    template <typename... Args>
    inline FaceBuilder &vertex(Args &&...args) {
      inds.push_back(builder.vertex(std::forward<Args>(args)...));
      return *this;
    }

    FaceBuilder &vertexRef(u32 ind) {
      inds.push_back(ind);
      return *this;
    }


    void end(void);

   private:
    friend class MeshBuilder;
    using MeshSubBuilder::MeshSubBuilder;

    std::vector<u32> inds;  // indices for the face
  };
}  // namespace ren