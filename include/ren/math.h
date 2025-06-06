#pragma once
#include <cmath>
#include <iomanip>
#include <iostream>
#include <math.h>


#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/string_cast.hpp>
#include <glm/gtx/io.hpp>
#include <glm/gtx/perpendicular.hpp>

namespace ren {

  // Linear interpolation between two vec3 points
  inline glm::vec3 lerp(const glm::vec3 &a, const glm::vec3 &b, float t) { return a + t * (b - a); }

  inline bool point_on_right_side_of_line(const glm::vec2 &a, const glm::vec2 &b, const glm::vec2 &p) {
    glm::vec2 ap = p - a;
    glm::vec2 ab_perp = glm::perp(glm::vec2(0), b - a);
    return glm::dot(ap, ab_perp) >= 0;
  }

  inline bool point_in_triangle(const glm::vec2 &a, const glm::vec2 &b, const glm::vec2 &c, const glm::vec2 &p) {
    bool side_ab = point_on_right_side_of_line(a, b, p);
    bool side_bc = point_on_right_side_of_line(b, c, p);
    bool side_ca = point_on_right_side_of_line(c, a, p);

    return side_ab and side_bc and side_ca;
  }


  inline glm::vec3 transform_point(const glm::mat4 &m, const glm::vec3 &v) { return glm::vec3(m * glm::vec4(v, 1.0f)); }

  inline glm::vec3 transform_vector(const glm::mat4 &m, const glm::vec3 &v) {
    return glm::vec3(m * glm::vec4(v, 0.0f));
  }
}  // namespace ren
