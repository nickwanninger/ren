#include <ren/assets/Mesh.h>
#include <tinygltf/tiny_gltf.h>
#include <tinyobjloader/tiny_obj_loader.h>

namespace ren {


  Mesh::Mesh(const std::string &name, const std::vector<Vertex> &vertices,
             const std::vector<u32> &indices)
      : name(name)
      , vertices(vertices)
      , indices(indices) {
    REN_PROFILE_FUNCTION();

    // Create the vertex buffer.
    vertexBuffer = makeRef<VertexBuffer<Vertex>>(vertices);
    vertexBuffer->setName(fmt::format("Mesh: {} Vertex Buffer", name));

    // Create the index buffer.
    indexBuffer = makeRef<IndexBuffer>(indices);
    indexBuffer->setName(fmt::format("Mesh: {} Index Buffer", name));
  }

  Mesh::~Mesh() {}


}  // namespace ren