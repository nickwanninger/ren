#include <ren/RenderTarget.h>

using namespace ren;

RenderTarget::RenderTarget(int width, int height)
    : width(width), height(height) {
  pixels.resize(width * height);
  depth.resize(width * height);
}

// Gamma correction and tone mapping
static inline uint32_t linear_to_srgb(const glm::vec3 &linear_color) {
  // Clamp values to [0, 1] range (tone mapping)
  float r = std::max(0.0f, std::min(1.0f, linear_color.r));
  float g = std::max(0.0f, std::min(1.0f, linear_color.g));
  float b = std::max(0.0f, std::min(1.0f, linear_color.b));

  // Apply sRGB gamma correction (approximation: gamma = 2.2)
  // More accurate sRGB conversion would use piecewise function
  // r = std::pow(r, 1.0f / 2.2f);
  // g = std::pow(g, 1.0f / 2.2f);
  // b = std::pow(b, 1.0f / 2.2f);

  // Convert to 8-bit integers and pack into 32-bit RGBA
  uint8_t r8 = (uint8_t)(r * 255.0f + 0.5f);
  uint8_t g8 = (uint8_t)(g * 255.0f + 0.5f);
  uint8_t b8 = (uint8_t)(b * 255.0f + 0.5f);
  uint8_t a8 = 255; // Full alpha

  // Pack into 32-bit value (RGBA format)
  return (uint32_t(a8) << 24) | (uint32_t(b8) << 16) | (uint32_t(g8) << 8) |
         uint32_t(r8);
}

void RenderTarget::quantize_screen(uint32_t *bitmap) const {
#pragma omp parallel for schedule(static)
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      bitmap[y * width + x] = linear_to_srgb(pixels[y * width + x]);
    }
  }
}

void RenderTarget::draw_line(glm::vec2 p0, glm::vec2 p1, glm::vec3 color) {

  p0 = to_device(p0);
  p1 = to_device(p1);

  // Bresenham's line algorithm
  int x0 = (int)p0.x;
  int y0 = (int)p0.y;
  int x1 = (int)p1.x;
  int y1 = (int)p1.y;

  // horrizontal optimization
  if (x0 == x1) {
    auto end = std::min(height, std::max(y0, y1));
    for (int y = std::max(0, std::min(y0, y1)); y < end; y++) {
      draw_pixel(x0, y, color);
    }
    return;
  }
  // vertical line optimization
  if (y0 == y1) {
    auto end = std::min(width, std::max(x0, x1));
    for (int x = std::max(0, std::min(x0, x1)); x < end; x++) {
      draw_pixel(x, y0, color);
    }
    return;
  }

  /* Draw a straight line from (x0,y0) to (x1,y1) with given color
   * Parameters:
   *      x0: x-coordinate of starting point of line. The x-coordinate of
   *          the top-left of the screen is 0. It increases to the right.
   *      y0: y-coordinate of starting point of line. The y-coordinate of
   *          the top-left of the screen is 0. It increases to the bottom.
   *      x1: x-coordinate of ending point of line. The x-coordinate of
   *          the top-left of the screen is 0. It increases to the right.
   *      y1: y-coordinate of ending point of line. The y-coordinate of
   *          the top-left of the screen is 0. It increases to the bottom.
   *      color: color value for line
   */
  short steep = abs(y1 - y0) > abs(x1 - x0);
  if (steep) {
    std::swap(x0, y0);
    std::swap(x1, y1);
  }

  if (x0 > x1) {
    std::swap(x0, x1);
    std::swap(y0, y1);
  }

  short dx, dy;
  dx = x1 - x0;
  dy = abs(y1 - y0);

  short err = dx / 2;
  short ystep;

  if (y0 < y1) {
    ystep = 1;
  } else {
    ystep = -1;
  }

  for (; x0 <= x1; x0++) {
    if (steep) {
      draw_pixel(y0, x0, color);
    } else {
      draw_pixel(x0, y0, color);
    }
    err -= dy;
    if (err < 0) {
      y0 += ystep;
      err += dx;
    }
  }
}

// Fragment shader interface - receives barycentric coordinates and vertex
// positions
glm::vec3 shader(const glm::vec2 &pixel_pos, const glm::vec3 &bary_coords,
              const glm::vec2 &v0, const glm::vec2 &v1, const glm::vec2 &v2) {
  // Example: interpolate colors based on barycentric coordinates
  // In a real system, you'd have vertex attributes (colors, UVs, normals, etc.)
  glm::vec3 color0(1.0f, 0.0f, 0.0f); // Red at vertex 0
  glm::vec3 color1(0.0f, 1.0f, 0.0f); // Green at vertex 1
  glm::vec3 color2(0.0f, 0.0f, 1.0f); // Blue at vertex 2

  return glm::vec3(color0.x * bary_coords.x + color1.x * bary_coords.y +
                    color2.x * bary_coords.z,
                color0.y * bary_coords.x + color1.y * bary_coords.y +
                    color2.y * bary_coords.z,
                color0.z * bary_coords.x + color1.z * bary_coords.y +
                    color2.z * bary_coords.z);
}

// Edge function for barycentric coordinate calculation
float edge_function(const glm::vec2 &a, const glm::vec2 &b, const glm::vec2 &c) {
  return (c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x);
}

// Calculate barycentric coordinates
glm::vec3 barycentric(const glm::vec2 &p, const glm::vec2 &a, const glm::vec2 &b,
                   const glm::vec2 &c) {
  float area = edge_function(a, b, c);
  if (std::abs(area) < 1e-6f)
    return glm::vec3(0, 0, 0); // Degenerate triangle

  float w0 = edge_function(b, c, p) / area;
  float w1 = edge_function(c, a, p) / area;
  float w2 = edge_function(a, b, p) / area;

  return glm::vec3(w0, w1, w2);
}

// Backface culling check - call this before rasterize()
bool is_front_facing(const glm::vec2 &a, const glm::vec2 &b, const glm::vec2 &c) {
  // Calculate signed area (cross product of two edges)
  // Positive area = counter-clockwise = front-facing (assuming CCW front faces)
  float signed_area = edge_function(a, b, c);
  return signed_area > 0.0f;
}

void RenderTarget::rasterize(glm::vec2 a, glm::vec2 b, glm::vec2 c) {
  // glm::vec3 color = glm::vec3(1.0f, 1.0f, 1.0f);
  // if (not is_front_facing(a, b, c)) {
  //   color.r = 1.0f;
  //   color.g = 0.0f;
  //   color.b = 0.0f;
  // }
  //   // color = glm::vec3(0.0f, 0.0f, 1.0f);

  // draw_line(a, b, color);
  // draw_line(b, c, color);
  // draw_line(c, a, color);
  // return;

  // Backface culling: skip rasterization if triangle is back-facing
  if (not is_front_facing(a, b, c))
    return;
  a = to_device(a);
  b = to_device(b);
  c = to_device(c);

  const float left = 0;
  const float right = width - left;
  const float top = 0;
  const float bottom = height - top;

  // get bounding box of the triangle (clamped to the screen bounds)
  glm::vec2 min_xy = glm::vec2(std::min({a.x, b.x, c.x}), std::min({a.y, b.y, c.y}));
  glm::vec2 max_xy = glm::vec2(std::max({a.x, b.x, c.x}), std::max({a.y, b.y, c.y}));
  min_xy.x = std::max(min_xy.x, left);
  min_xy.y = std::max(min_xy.y, top);
  max_xy.x = std::min(max_xy.x, right);
  max_xy.y = std::min(max_xy.y, bottom);

  // draw_box(from_device(min_xy), from_device(max_xy),
  // glm::vec3(1.0f, 1.0f, 1.0f));

  if (min_xy.x > max_xy.x || min_xy.y > max_xy.y) {
    return;
  }

  int min_x = std::floor(min_xy.x);
  int min_y = std::floor(min_xy.y);
  int max_x = std::ceil(max_xy.x);
  int max_y = std::ceil(max_xy.y);

  // Calculate triangle area once (for barycentric coordinate normalization)
  float area = edge_function(a, b, c);
  if (std::abs(area) < 1e-6f)
    return; // Skip degenerate triangles

  for (int y = min_y; y <= max_y; y++) {
    float scan_y = y + 0.5f;

    for (int x = min_x; x <= max_x; x++) {
      glm::vec2 pixel_pos(x + 0.5f, scan_y);

      // Calculate barycentric coordinates
      float w0 = edge_function(b, c, pixel_pos) / area;
      float w1 = edge_function(c, a, pixel_pos) / area;
      float w2 = edge_function(a, b, pixel_pos) / area;

      // Check if point is inside triangle (all barycentric coords >= 0)
      if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
        // if (x_start > max_x)
        //   x_start = x; // First valid pixel
        // x_end = x;     // Update last valid pixel

        // Call shader and set pixel
        glm::vec3 bary_coords(w0, w1, w2);
        glm::vec3 color = shader(pixel_pos, bary_coords, a, b, c);
        pix(x, y) = color;
      }
    }
  }
}
