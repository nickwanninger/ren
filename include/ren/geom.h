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

    void add_triangle(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c) {
      Vertex v0 = {glm::vec4(a, 1.0f), glm::vec3(0.0f), glm::vec2(0.0f), glm::vec3(1.0f)};
      Vertex v1 = {glm::vec4(b, 1.0f), glm::vec3(0.0f), glm::vec2(0.0f), glm::vec3(1.0f)};
      Vertex v2 = {glm::vec4(c, 1.0f), glm::vec3(0.0f), glm::vec2(0.0f), glm::vec3(1.0f)};
      add_triangle(v0, v1, v2);
    }
  };
}  // namespace ren
