#pragma once

#include <ren/math.h>
#include <vector>

namespace ren {
// A render target is a simple buffer of floating point RGB pixels.
// Somewhere down the line we quantize/render this to a texture or framebuffer.
class RenderTarget {
public:
  int width, height;
  float3 off_screen; // This is what we return if pix() is called out of bounds.
  std::vector<float3> pixels;
  std::vector<float> depth; // Depth buffer.

  RenderTarget(int width, int height);

  // Access pixel at (x, y)
  inline float3 &pix(int x, int y) {
    if (x < 0 || x >= width || y < 0 || y >= height) {
      // Out of bounds access, return off_screen color
      return off_screen;
    }
    return pixels[y * width + x];
  }

  inline const float3 &pix(int x, int y) const {
    if (x < 0 || x >= width || y < 0 || y >= height) {
      return off_screen;
    }
    return pixels[y * width + x];
  }

  template <typename Fn> inline void for_each_pixel(Fn fn) {
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        fn(pix(x, y), float2(x, y));
      }
    }
  }

  template <typename Fn>
  inline void for_each_pixel(float2 topleft, float2 bottomright, Fn fn) {
    // clamp the rectangle to the bounds of the render target
    topleft.x = std::max(0.0f, std::min((float)width, topleft.x));
    topleft.y = std::max(0.0f, std::min((float)height, topleft.y));
    bottomright.x = std::max(0.0f, std::min((float)width, bottomright.x));
    bottomright.y = std::max(0.0f, std::min((float)height, bottomright.y));

    for (int y = (int)topleft.y; y < (int)bottomright.y; y++) {
      for (int x = (int)topleft.x; x < (int)bottomright.x; x++) {
        fn(pix(x, y), float2(x, y));
      }
    }
  }

  void quantize_screen(uint32_t *bitmap) const;

  // other helpful debug methods. These should not generally be used.
  void clear(float3 color = float3(0.0f, 0.0f, 0.0f)) {
    for (auto &p : pixels)
      p = color;
  }

  void draw_line(float2 p0, float2 p1, float3 color);
  void draw_pixel(int x, int y, float3 color) { pix(x, y) = color; }
  void draw_box(float2 topleft, float2 bottomright, float3 color) {
    draw_line(topleft, float2(bottomright.x, topleft.y), color);
    draw_line(float2(bottomright.x, topleft.y), bottomright, color);
    draw_line(bottomright, float2(topleft.x, bottomright.y), color);
    draw_line(float2(topleft.x, bottomright.y), topleft, color);
  }

  void rasterize(float2 a, float2 b, float2 c);
};
} // namespace ren
