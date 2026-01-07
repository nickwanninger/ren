#pragma once

#include <glm/glm.hpp>
#include <imgui/imgui.h>
#include <cmath>

namespace ren {


  inline constexpr glm::vec3 hexColor(uint32_t hexValue) {
    float r = ((hexValue >> 16) & 0xFF) / 255.0f;
    float g = ((hexValue >> 8) & 0xFF) / 255.0f;
    float b = (hexValue & 0xFF) / 255.0f;
    return glm::vec3(r, g, b);
  }

  inline constexpr ImVec4 hexImColor(uint32_t hexValue, float alpha = 1.0f) {
    glm::vec3 color = hexColor(hexValue);
    return ImVec4(color.r, color.g, color.b, alpha);
  }


  struct Color {
    // These are in linear
    float r, g, b, a;

    // Construct from hex RGB (sRGB) + alpha
    constexpr Color(uint32_t rgb, float a = 1.0f)
        : r(srgb_to_linear(((rgb >> 16) & 0xFF) / 255.0f))
        , g(srgb_to_linear(((rgb >> 8) & 0xFF) / 255.0f))
        , b(srgb_to_linear((rgb & 0xFF) / 255.0f))
        , a(a) {}

    // Construct from linear RGBA floats
    Color(float r, float g, float b, float a = 1.0f)
        : r(r)
        , g(g)
        , b(b)
        , a(a) {}

    // Convert to ImVec4 in sRGB space (for ImGui)
    operator ImVec4() const { return ImVec4(r, g, b, a); }

   private:
    static constexpr float srgb_to_linear(float c) {
      // return c;
      return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }
  };

#define COLOR_WHITE Color(0xFFFFFF)
#define COLOR_RED Color(0xFFF5C60)
#define COLOR_YELLOW Color(0xFAC800)
#define COLOR_GREEN Color(0x35C759)
#define COLOR_BLUE Color(0x9DCEFF)

#define COLOR_FRAMEBG Color(0x353535)
#define COLOR_FRAMEFG COLOR_WHITE

}  // namespace ren