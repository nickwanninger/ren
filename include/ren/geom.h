#pragma once


#include <vector>

#include <ren/math.h>


namespace ren {

  struct Vertex {
    glm::vec4 position;  // Position of the vertex
    glm::vec3 normal;    // Normal vector in world space
    glm::vec2 texcoord;  // Texture coordinates
    glm::vec3 color;     // Vertex color (optional, can be used for lighting or shading)
  };


  struct Triangle {
    Vertex vertices[3];  // Three vertices of the triangle
  };

  struct Mesh {
    std::vector<Triangle> triangles;  // List of triangles in the mesh

    // Add a triangle to the mesh
    void add_triangle(const Vertex &v0, const Vertex &v1, const Vertex &v2) { triangles.push_back({{v0, v1, v2}}); }
  };
}  // namespace ren
