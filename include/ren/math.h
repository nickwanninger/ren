#pragma once

#include <cmath>
#include <iostream>

namespace ren {

struct float2 {
  float x, y;

  // Constructors
  float2() : x(0), y(0) {}
  float2(float val) : x(val), y(val) {}
  float2(float x_, float y_) : x(x_), y(y_) {}

  // Array access
  float &operator[](int i) { return (&x)[i]; }
  const float &operator[](int i) const { return (&x)[i]; }

  // Arithmetic operators
  float2 operator+(const float2 &v) const { return float2(x + v.x, y + v.y); }
  float2 operator-(const float2 &v) const { return float2(x - v.x, y - v.y); }
  float2 operator*(float s) const { return float2(x * s, y * s); }
  float2 operator/(float s) const { return float2(x / s, y / s); }

  // Component-wise multiplication
  float2 operator*(const float2 &v) const { return float2(x * v.x, y * v.y); }

  // Compound assignment
  float2 &operator+=(const float2 &v) {
    x += v.x;
    y += v.y;
    return *this;
  }
  float2 &operator-=(const float2 &v) {
    x -= v.x;
    y -= v.y;
    return *this;
  }
  float2 &operator*=(float s) {
    x *= s;
    y *= s;
    return *this;
  }
  float2 &operator/=(float s) {
    x /= s;
    y /= s;
    return *this;
  }
  float2 &operator*=(const float2 &v) {
    x *= v.x;
    y *= v.y;
    return *this;
  }

  // Unary operators
  float2 operator-() const { return float2(-x, -y); }

  // Vector operations
  float dot(const float2 &v) const { return x * v.x + y * v.y; }

  // 2D cross product (returns scalar - z component of 3D cross)
  float cross(const float2 &v) const { return x * v.y - y * v.x; }

  float length() const { return std::sqrt(x * x + y * y); }
  float length_squared() const { return x * x + y * y; }

  float2 normalized() const {
    float len = length();
    return len > 0 ? *this / len : float2(0);
  }

  void normalize() { *this = normalized(); }

  // Perpendicular vector (rotated 90 degrees counter-clockwise)
  float2 perpendicular() const { return float2(-y, x); }

  // Angle from positive x-axis
  float angle() const { return std::atan2(y, x); }

  // Distance to another point
  float distance(const float2 &other) const { return (*this - other).length(); }

  // Linear interpolation
  float2 lerp(const float2 &other, float t) const {
    return *this + (other - *this) * t;
  }

  // Rotation
  float2 rotated(float angle) const {
    float c = std::cos(angle), s = std::sin(angle);
    return float2(x * c - y * s, x * s + y * c);
  }
};

// Non-member operators
inline float2 operator*(float s, const float2 &v) { return v * s; }

// Stream output
inline std::ostream &operator<<(std::ostream &os, const float2 &v) {
  return os << "(" << v.x << ", " << v.y << ")";
}

struct float3 {
  union {
    struct {
      float x, y, z;
    };

    struct {
      float r, g, b;
    };

    float vals[3];
  };

  // Constructors
  float3() : x(0), y(0), z(0) {}
  float3(float val) : x(val), y(val), z(val) {}
  float3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

  inline float2 xy() { return float2(x, y); }

  // Array access
  float &operator[](int i) { return (&x)[i]; }
  const float &operator[](int i) const { return (&x)[i]; }

  // Arithmetic operators
  float3 operator+(const float3 &v) const {
    return float3(x + v.x, y + v.y, z + v.z);
  }
  float3 operator-(const float3 &v) const {
    return float3(x - v.x, y - v.y, z - v.z);
  }
  float3 operator*(float s) const { return float3(x * s, y * s, z * s); }
  float3 operator/(float s) const { return float3(x / s, y / s, z / s); }

  // Compound assignment
  float3 &operator+=(const float3 &v) {
    x += v.x;
    y += v.y;
    z += v.z;
    return *this;
  }
  float3 &operator-=(const float3 &v) {
    x -= v.x;
    y -= v.y;
    z -= v.z;
    return *this;
  }
  float3 &operator*=(float s) {
    x *= s;
    y *= s;
    z *= s;
    return *this;
  }
  float3 &operator/=(float s) {
    x /= s;
    y /= s;
    z /= s;
    return *this;
  }

  // Unary operators
  float3 operator-() const { return float3(-x, -y, -z); }

  // Vector operations
  float dot(const float3 &v) const { return x * v.x + y * v.y + z * v.z; }
  float3 cross(const float3 &v) const {
    return float3(y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x);
  }

  float length() const { return std::sqrt(x * x + y * y + z * z); }
  float length_squared() const { return x * x + y * y + z * z; }

  float3 normalized() const {
    float len = length();
    return len > 0 ? *this / len : float3(0);
  }

  void normalize() { *this = normalized(); }
};

// 4x4 Matrix class (column-major storage like OpenGL)
struct matrix4 {
  float m[16];

  // Constructors
  matrix4() { identity(); }
  matrix4(float diagonal) {
    for (int i = 0; i < 16; i++)
      m[i] = 0;
    m[0] = m[5] = m[10] = m[15] = diagonal;
  }

  // Array access - m[col][row] style
  float *operator[](int col) { return &m[col * 4]; }
  const float *operator[](int col) const { return &m[col * 4]; }

  // Element access - (row, col)
  float &at(int row, int col) { return m[col * 4 + row]; }
  const float &at(int row, int col) const { return m[col * 4 + row]; }

  void identity() {
    for (int i = 0; i < 16; i++)
      m[i] = 0;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
  }

  // Matrix multiplication
  matrix4 operator*(const matrix4 &other) const {
    matrix4 result;
    for (int col = 0; col < 4; col++) {
      for (int row = 0; row < 4; row++) {
        float sum = 0;
        for (int k = 0; k < 4; k++) {
          sum += at(row, k) * other.at(k, col);
        }
        result.at(row, col) = sum;
      }
    }
    return result;
  }

  matrix4 &operator*=(const matrix4 &other) { return *this = *this * other; }

  // Vector transformation (homogeneous coordinates)
  float3 transform_point(const float3 &p) const {
    float x = at(0, 0) * p.x + at(0, 1) * p.y + at(0, 2) * p.z + at(0, 3);
    float y = at(1, 0) * p.x + at(1, 1) * p.y + at(1, 2) * p.z + at(1, 3);
    float z = at(2, 0) * p.x + at(2, 1) * p.y + at(2, 2) * p.z + at(2, 3);
    float w = at(3, 0) * p.x + at(3, 1) * p.y + at(3, 2) * p.z + at(3, 3);
    return w != 0 ? float3(x / w, y / w, z / w) : float3(x, y, z);
  }

  // Vector transformation (no translation)
  float3 transform_vector(const float3 &v) const {
    return float3(at(0, 0) * v.x + at(0, 1) * v.y + at(0, 2) * v.z,
                  at(1, 0) * v.x + at(1, 1) * v.y + at(1, 2) * v.z,
                  at(2, 0) * v.x + at(2, 1) * v.y + at(2, 2) * v.z);
  }

  // Common transformations
  static matrix4 translation(const float3 &t) {
    matrix4 result;
    result.at(0, 3) = t.x;
    result.at(1, 3) = t.y;
    result.at(2, 3) = t.z;
    return result;
  }

  static matrix4 scale(const float3 &s) {
    matrix4 result;
    result.at(0, 0) = s.x;
    result.at(1, 1) = s.y;
    result.at(2, 2) = s.z;
    return result;
  }

  static matrix4 rotation_x(float angle) {
    matrix4 result;
    float c = std::cos(angle), s = std::sin(angle);
    result.at(1, 1) = c;
    result.at(1, 2) = -s;
    result.at(2, 1) = s;
    result.at(2, 2) = c;
    return result;
  }

  static matrix4 rotation_y(float angle) {
    matrix4 result;
    float c = std::cos(angle), s = std::sin(angle);
    result.at(0, 0) = c;
    result.at(0, 2) = s;
    result.at(2, 0) = -s;
    result.at(2, 2) = c;
    return result;
  }

  static matrix4 rotation_z(float angle) {
    matrix4 result;
    float c = std::cos(angle), s = std::sin(angle);
    result.at(0, 0) = c;
    result.at(0, 1) = -s;
    result.at(1, 0) = s;
    result.at(1, 1) = c;
    return result;
  }

  // Perspective projection
  static matrix4 perspective(float fov_y, float aspect, float near_z,
                             float far_z) {
    matrix4 result(0);
    float tan_half_fov = std::tan(fov_y * 0.5f);
    result.at(0, 0) = 1.0f / (aspect * tan_half_fov);
    result.at(1, 1) = 1.0f / tan_half_fov;
    result.at(2, 2) = -(far_z + near_z) / (far_z - near_z);
    result.at(2, 3) = -(2.0f * far_z * near_z) / (far_z - near_z);
    result.at(3, 2) = -1.0f;
    return result;
  }

  // Look-at view matrix
  static matrix4 look_at(const float3 &eye, const float3 &center,
                         const float3 &up) {
    float3 f = (center - eye).normalized();
    float3 s = f.cross(up).normalized();
    float3 u = s.cross(f);

    matrix4 result;
    result.at(0, 0) = s.x;
    result.at(0, 1) = s.y;
    result.at(0, 2) = s.z;
    result.at(1, 0) = u.x;
    result.at(1, 1) = u.y;
    result.at(1, 2) = u.z;
    result.at(2, 0) = -f.x;
    result.at(2, 1) = -f.y;
    result.at(2, 2) = -f.z;
    result.at(0, 3) = -s.dot(eye);
    result.at(1, 3) = -u.dot(eye);
    result.at(2, 3) = f.dot(eye);
    return result;
  }
};

// Stream output for matrix
inline std::ostream &operator<<(std::ostream &os, const matrix4 &mat) {
  for (int row = 0; row < 4; row++) {
    os << "[";
    for (int col = 0; col < 4; col++) {
      os << mat.at(row, col);
      if (col < 3)
        os << ", ";
    }
    os << "]";
    if (row < 3)
      os << "\n";
  }
  return os;
}

inline bool point_on_right_side_of_line(const float2 &a, const float2 &b,
                                        const float2 &p) {
  float2 ap = p - a;
  float2 ab_perp = (b - a).perpendicular();
  return ap.dot(ab_perp) >= 0;
}

inline bool point_in_triangle(const float2 &a, const float2 &b, const float2 &c,
                              const float2 &p) {
  bool side_ab = point_on_right_side_of_line(a, b, p);
  bool side_bc = point_on_right_side_of_line(b, c, p);
  bool side_ca = point_on_right_side_of_line(c, a, p);

  return side_ab and side_bc and side_ca;
}

} // namespace ren
