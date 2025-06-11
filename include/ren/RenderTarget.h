#pragma once

#include <ren/math.h>
#include <glm/glm.hpp>
#include <vector>


namespace ren {
  // A render target is a simple buffer of floating point RGB pixels.
  // Somewhere down the line we quantize/render this to a texture or framebuffer.
  class RenderTarget {
   public:
    long triangles;  // how many triangles were rasterized
    int width, height;
    glm::vec3 off_screen;  // This is what we return if pix() is called out of bounds.
    std::vector<glm::vec3> pixels;
    std::vector<float> depth;  // Depth buffer.

    RenderTarget(int width, int height);

    // Access pixel at (x, y)
    inline glm::vec3 &pix(int x, int y) {
      if (x < 0 || x >= width || y < 0 || y >= height) {
        // Out of bounds access, return off_screen color
        return off_screen;
      }
      return pixels[y * width + x];
    }

    inline const glm::vec3 &pix(int x, int y) const {
      if (x < 0 || x >= width || y < 0 || y >= height) { return off_screen; }
      return pixels[y * width + x];
    }

    template <typename Fn>
    inline void for_each_pixel(Fn fn) {
      for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
          fn(pix(x, y), glm::vec2(x, y));
        }
      }
    }

    template <typename Fn>
    inline void for_each_pixel(glm::vec2 topleft, glm::vec2 bottomright, Fn fn) {
      // clamp the rectangle to the bounds of the render target
      topleft.x = std::max(0.0f, std::min((float)width, topleft.x));
      topleft.y = std::max(0.0f, std::min((float)height, topleft.y));
      bottomright.x = std::max(0.0f, std::min((float)width, bottomright.x));
      bottomright.y = std::max(0.0f, std::min((float)height, bottomright.y));

      for (int y = (int)topleft.y; y < (int)bottomright.y; y++) {
        for (int x = (int)topleft.x; x < (int)bottomright.x; x++) {
          fn(pix(x, y), glm::vec2(x, y));
        }
      }
    }

    glm::vec2 to_device(glm::vec2 p) const {
      // Convert from normalized device coordinates to pixel coordinates
      return glm::vec2((int)((p.x + 1.0f) * 0.5f * width), (int)((-p.y + 1.0f) * 0.5f * height));
    }

    glm::vec2 from_device(glm::vec2 p) const {
      // Convert from pixel coordinates to normalized device coordinates
      return glm::vec2((p.x / width) * 2.0f - 1.0f, -((p.y / height) * 2.0f - 1.0f));
    }

    void quantize_screen(uint32_t *bitmap) const;

    // other helpful debug methods. These should not generally be used.
    void clear(glm::vec3 color = glm::vec3(0.0f, 0.0f, 0.0f)) {
      for (auto &p : pixels)
        p = color;

      for (auto &d : depth)
        d = 0;
    }

    void draw_line(glm::vec2 p0, glm::vec2 p1, glm::vec3 color);
    void draw_circle(glm::vec2 center, float radius, glm::vec3 color) {
      for (float angle = 0; angle < 2 * M_PI; angle += 0.01f) {
        glm::vec2 p0 = center + glm::vec2(cos(angle), sin(angle)) * radius;
        glm::vec2 p1 = center + glm::vec2(cos(angle + 0.01f), sin(angle + 0.01f)) * radius;
        draw_line(p0, p1, color);
      }
    }
    void draw_pixel(int x, int y, glm::vec3 color) { pix(x, y) = color; }
    void draw_box(glm::vec2 topleft, glm::vec2 bottomright, glm::vec3 color) {
      draw_line(topleft, glm::vec2(bottomright.x, topleft.y), color);
      draw_line(glm::vec2(bottomright.x, topleft.y), bottomright, color);
      draw_line(bottomright, glm::vec2(topleft.x, bottomright.y), color);
      draw_line(glm::vec2(topleft.x, bottomright.y), topleft, color);
    }

    // rasterize triangles in 2d space (screen space) without clipping checks
    void rasterize(glm::vec4 a, glm::vec4 b, glm::vec4 c);


    // Given a triangle in clip space, rasterize it to the render target.
    // This handles *near* clipping, but not edge or anything else.
    // That will be another step later, but I don't see why optimizing it
    // would be all that useful right now.
    void rasterizeClip(glm::vec4 a, glm::vec4 b, glm::vec4 c);
  };
}  // namespace ren
