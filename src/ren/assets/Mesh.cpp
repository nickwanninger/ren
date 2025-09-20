#include <ren/assets/Mesh.h>
#include <ren/assets/MegaMeshBuffer.h>
#include <ren/core/Application.h>


namespace ren {


  Mesh::Mesh(const std::string &name, const std::vector<Vertex> &vertices,
             const std::vector<u32> &indices)
      : name(name)
      , vertexCount(vertices.size())
      , indexCount(indices.size()) {
    REN_PROFILE_FUNCTION();

    for (auto &vert : vertices) {
      aabb.update(vert.pos);
    }

    this->megaHandle = ren::resource<ren::MegaMeshBuffer>().allocate(vertices, indices);
  }

  Mesh::~Mesh() {}


}  // namespace ren