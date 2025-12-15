#include <ren/assets/MeshBuilder.h>


namespace ren {

  void MeshData::dumpObj(void) {
    for (const auto &v : vertices) {
      printf("v %f %f %f\n", v.pos.x, v.pos.y, v.pos.z);
    }
    for (size_t i = 0; i < indices.size(); i += 3) {
      printf("f %u %u %u\n", indices[i] + 1, indices[i + 1] + 1, indices[i + 2] + 1);
    }
  }


  MeshBuilder::MeshBuilder(void) {
    // Allocate the mesh data.
    data = make<MeshData>();
  }


  void MeshBuilder::vertex(u32 idx, const ren::Vertex &v) {
    if (idx >= data->vertices.size()) {
      throw std::out_of_range("MeshBuilder::vertex: index out of range");
    }
    data->vertices[idx] = v;
  }

  void MeshBuilder::index(u32 idx) {
    // Simply add the index to the list.
    data->indices.push_back(idx);
  }


  ref<MeshData> MeshBuilder::stampOut(void) {
    auto stamped = make<MeshData>();
    *stamped = *data;
    return stamped;
  }




  // Face builder.
  FaceBuilder MeshBuilder::beginFace(void) { return FaceBuilder(*this); }


  void FaceBuilder::end(void) {
    // Inds is a list of vertex indices for the face.
    // Triangulate it using ear clipping algorithm.
    if (inds.size() < 3) {
      throw std::runtime_error("FaceBuilder::end: face must have at least 3 vertices");
    }

    // Trivial case: already a triangle
    if (inds.size() == 3) {
      builder.index(inds[0]);
      builder.index(inds[1]);
      builder.index(inds[2]);
      inds.clear();
      return;
    }

    // Ear clipping triangulation for polygons with 4+ vertices
    std::vector<size_t> polygon(inds.begin(), inds.end());

    while (polygon.size() > 3) {
      bool found_ear = false;

      // Find an ear (a triangle that is inside the polygon)
      for (size_t i = 0; i < polygon.size(); i++) {
        size_t prev = (i + polygon.size() - 1) % polygon.size();
        size_t next = (i + 1) % polygon.size();

        u32 v0 = inds[polygon[prev]];
        u32 v1 = inds[polygon[i]];
        u32 v2 = inds[polygon[next]];

        const ren::Vertex &pv = builder.getVertex(v0);
        const ren::Vertex &cv = builder.getVertex(v1);
        const ren::Vertex &nv = builder.getVertex(v2);

        // Check if this vertex is convex (cross product test)
        glm::vec3 edge1 = cv.pos - pv.pos;
        glm::vec3 edge2 = nv.pos - cv.pos;
        glm::vec3 cross = glm::cross(edge1, edge2);

        // If cross product Z is positive, vertex is convex
        if (cross.z <= 0.0f) { continue; }

        // Check if any other vertices are inside this triangle
        bool is_ear = true;
        for (size_t j = 0; j < polygon.size(); j++) {
          if (j == prev || j == i || j == next) { continue; }

          u32 test_idx = inds[polygon[j]];
          const ren::Vertex &test_v = builder.getVertex(test_idx);

          // Point-in-triangle test using barycentric coordinates
          glm::vec3 p = test_v.pos;
          glm::vec3 a = pv.pos;
          glm::vec3 b = cv.pos;
          glm::vec3 c = nv.pos;

          glm::vec3 v0_tri = c - a;
          glm::vec3 v1_tri = b - a;
          glm::vec3 v2_tri = p - a;

          float dot00 = glm::dot(v0_tri, v0_tri);
          float dot01 = glm::dot(v0_tri, v1_tri);
          float dot02 = glm::dot(v0_tri, v2_tri);
          float dot11 = glm::dot(v1_tri, v1_tri);
          float dot12 = glm::dot(v1_tri, v2_tri);

          float inv_denom = 1.0f / (dot00 * dot11 - dot01 * dot01);
          float u = (dot11 * dot02 - dot01 * dot12) * inv_denom;
          float v = (dot00 * dot12 - dot01 * dot02) * inv_denom;

          // Point is inside triangle if u > 0, v > 0, u + v < 1
          if (u > 1e-6f && v > 1e-6f && u + v < 1.0f - 1e-6f) {
            is_ear = false;
            break;
          }
        }

        if (is_ear) {
          // Found an ear, output it as a triangle
          builder.index(v0);
          builder.index(v1);
          builder.index(v2);

          // Remove the ear vertex from the polygon
          polygon.erase(polygon.begin() + i);
          found_ear = true;
          break;
        }
      }

      if (!found_ear) {
        // Degenerate polygon, shouldn't happen but fall back to fan
        break;
      }
    }

    // Output the final triangle
    if (polygon.size() == 3) {
      builder.index(inds[polygon[0]]);
      builder.index(inds[polygon[1]]);
      builder.index(inds[polygon[2]]);
    }

    // Clear pending vertices for next face.
    inds.clear();
  }



}  // namespace ren
