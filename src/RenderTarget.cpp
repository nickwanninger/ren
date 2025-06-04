#include <ren/RenderTarget.h>

using namespace ren;

RenderTarget::RenderTarget(int width, int height)
    : width(width), height(height) {
  pixels.resize(width * height);
  depth.resize(width * height);
}

// Gamma correction and tone mapping
static inline uint32_t linear_to_srgb(const float3 &linear_color) {
  // Clamp values to [0, 1] range (tone mapping)
  float r = std::max(0.0f, std::min(1.0f, linear_color.x));
  float g = std::max(0.0f, std::min(1.0f, linear_color.y));
  float b = std::max(0.0f, std::min(1.0f, linear_color.z));

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

void RenderTarget::draw_line(float2 p0, float2 p1, float3 color) {

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
float3 shader(const float2 &pixel_pos, const float3 &bary_coords,
              const float2 &v0, const float2 &v1, const float2 &v2) {
  // Example: interpolate colors based on barycentric coordinates
  // In a real system, you'd have vertex attributes (colors, UVs, normals, etc.)
  float3 color0(1.0f, 0.0f, 0.0f); // Red at vertex 0
  float3 color1(0.0f, 1.0f, 0.0f); // Green at vertex 1
  float3 color2(0.0f, 0.0f, 1.0f); // Blue at vertex 2

  return float3(color0.x * bary_coords.x + color1.x * bary_coords.y +
                    color2.x * bary_coords.z,
                color0.y * bary_coords.x + color1.y * bary_coords.y +
                    color2.y * bary_coords.z,
                color0.z * bary_coords.x + color1.z * bary_coords.y +
                    color2.z * bary_coords.z);
}

// Edge function for barycentric coordinate calculation
float edge_function(const float2 &a, const float2 &b, const float2 &c) {
  return (c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x);
}

// Calculate barycentric coordinates
float3 barycentric(const float2 &p, const float2 &a, const float2 &b,
                   const float2 &c) {
  float area = edge_function(a, b, c);
  if (std::abs(area) < 1e-6f)
    return float3(0, 0, 0); // Degenerate triangle

  float w0 = edge_function(b, c, p) / area;
  float w1 = edge_function(c, a, p) / area;
  float w2 = edge_function(a, b, p) / area;

  return float3(w0, w1, w2);
}

// Backface culling check - call this before rasterize()
bool is_front_facing(const float2 &a, const float2 &b, const float2 &c) {
  // Calculate signed area (cross product of two edges)
  // Positive area = counter-clockwise = front-facing (assuming CCW front faces)
  float signed_area = edge_function(a, b, c);
  return signed_area > 0.0f;
}

void RenderTarget::rasterize(float2 a, float2 b, float2 c) {

  a = to_device(a);
  b = to_device(b);
  c = to_device(c);
  
  
  if (is_front_facing(a, b, c) == false) {
    // Backface culling: skip rasterization if triangle is back-facing
    return;
  }

  // Sort vertices by y-coordinate (top to bottom)
  float2 v0 = a, v1 = b, v2 = c;
  if (v0.y > v1.y)
    std::swap(v0, v1);
  if (v1.y > v2.y)
    std::swap(v1, v2);
  if (v0.y > v1.y)
    std::swap(v0, v1);

  // Calculate triangle area for barycentric coordinates
  float area = edge_function(a, b, c);
  if (std::abs(area) < 1e-6f)
    return;

  // Rasterize upper half (v0 to v1)
  if (v0.y != v1.y) {
    float inv_slope1 = (v1.x - v0.x) / (v1.y - v0.y);
    float inv_slope2 = (v2.x - v0.x) / (v2.y - v0.y);

    int start_y = (int)std::ceil(v0.y - 0.5f);
    int end_y = (int)std::floor(v1.y + 0.5f);

    for (int y = start_y; y <= end_y; y++) {
      float scan_y = y + 0.5f;
      float dy = scan_y - v0.y;

      float x1 = v0.x + inv_slope1 * dy;
      float x2 = v0.x + inv_slope2 * dy;

      if (x1 > x2)
        std::swap(x1, x2);

      int start_x = (int)std::ceil(x1 - 0.5f);
      int end_x = (int)std::floor(x2 + 0.5f);

      for (int x = start_x; x <= end_x; x++) {
        float2 pixel_pos(x + 0.5f, scan_y);
        float3 bary_coords = barycentric(pixel_pos, a, b, c);

        if (bary_coords.x >= 0 && bary_coords.y >= 0 && bary_coords.z >= 0) {
          float3 color = shader(pixel_pos, bary_coords, a, b, c);
          pix(x, y) = color;
        }
      }
    }
  }

  // Rasterize lower half (v1 to v2)
  if (v1.y != v2.y) {
    float inv_slope1 = (v2.x - v1.x) / (v2.y - v1.y);
    float inv_slope2 = (v2.x - v0.x) / (v2.y - v0.y);

    int start_y = (int)std::ceil(v1.y - 0.5f);
    int end_y = (int)std::floor(v2.y + 0.5f);

    for (int y = start_y; y <= end_y; y++) {
      float scan_y = y + 0.5f;

      float x1 = v1.x + inv_slope1 * (scan_y - v1.y);
      float x2 = v0.x + inv_slope2 * (scan_y - v0.y);

      if (x1 > x2)
        std::swap(x1, x2);

      int start_x = (int)std::ceil(x1 - 0.5f);
      int end_x = (int)std::floor(x2 + 0.5f);

      for (int x = start_x; x <= end_x; x++) {
        float2 pixel_pos(x + 0.5f, scan_y);
        float3 bary_coords = barycentric(pixel_pos, a, b, c);

        if (bary_coords.x >= 0 && bary_coords.y >= 0 && bary_coords.z >= 0) {
          float3 color = shader(pixel_pos, bary_coords, a, b, c);
          pix(x, y) = color;
        }
      }
    }
  }
}
