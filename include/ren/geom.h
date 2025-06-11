#pragma once


#include <vector>

#include <ren/math.h>


namespace ren {


  // A vertex is a point in 3D space (represented as a vec4 with w=1 until with additional attributes
  struct Vertex {
    glm::vec4 position;  // Position of the vertex
    glm::vec3 normal;    // Normal vector in world space
    glm::vec2 uv;        // Texture coordinates
    glm::vec3 color;     // Vertex color (optional, can be used for lighting or shading)

    Vertex(const glm::vec4 &pos, const glm::vec3 &norm = glm::vec3(0.0f), const glm::vec2 &uv = glm::vec2(0.0f),
        const glm::vec3 &col = glm::vec3(1.0f))
        : position(pos)
        , normal(norm)
        , uv(uv)
        , color(col) {}


    // apply a transformation matrix to the vertex position
    Vertex transformed(const glm::mat4 &m) const { return {m * position, transform_vector(m, normal), uv, color}; }

    operator glm::vec4() const { return position; }
  };

  // multiply vertex by a matrix
  inline Vertex operator*(const glm::mat4 &m, const Vertex &v) {
    // Transform the vertex position and normal using the matrix
    return v.transformed(m);
  }


  // A triangle is defined by three vertices (which have their own attributes)
  struct Triangle {
    Vertex v[3];  // Three vertices of the triangle



    glm::vec3 normal() const {
      // compute the normal of the triangle. Assume a, b, c are in counter-clockwise order
      glm::vec3 edge1 = v[1].position - v[0].position;  // First edge
      glm::vec3 edge2 = v[2].position - v[0].position;  // Second edge
      glm::vec3 n = glm::cross(edge2, edge1);           // Cross product gives the normal vector
      return glm::normalize(n);                         // Normalize the normal vector
    }


    void compute_normals() {
      // Compute normals for each vertex based on the triangle's normal
      glm::vec3 n = normal();
      for (int i = 0; i < 3; i++)
        v[i].normal = n;  // Set the normal for each vertex
    }
  };



  struct Mesh {
    std::vector<Triangle> triangles;  // List of triangles in the mesh

    // Add a triangle to the mesh
    void add_triangle(const Vertex &v0, const Vertex &v1, const Vertex &v2) { triangles.push_back({v0, v1, v2}); }
  };
}  // namespace ren
