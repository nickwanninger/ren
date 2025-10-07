#include <ren/core/DebugLines.hpp>
#include <imgui/imgui.h>
#include <ren/core/AutoPlugin.h>

namespace ren {


  struct DebugLine {
    glm::vec3 a, b;
    glm::vec3 color;
    float thickness = 1.0f;
  };

  std::vector<DebugLine> g_debugLines;



  // dispatch the draw calls for the debug lines
  void debugLine(glm::vec3 a, glm::vec3 b, glm::vec3 color, float thickness) {
    g_debugLines.push_back({a, b, color, thickness});
  }

  void debugLineLocal(const glm::mat4 &transform, glm::vec3 a, glm::vec3 b, glm::vec3 color,
                      float thickness) {
    glm::vec4 localA = transform * glm::vec4(a, 1.0f);
    glm::vec4 localB = transform * glm::vec4(b, 1.0f);
    g_debugLines.push_back({glm::vec3(localA), glm::vec3(localB), color, thickness});
  }


  void debugRay(glm::vec3 origin, glm::vec3 direction, glm::vec3 color, float thickness) {
    g_debugLines.push_back({origin, origin + direction, color, thickness});
  }

  void renderDebugLines(glm::mat4 view, glm::mat4 proj) {
    ImDrawList *draw_list = ImGui::GetBackgroundDrawList();

    auto projectToScreen = [&](glm::vec3 point) {
      glm::vec4 clipSpace = proj * view * glm::vec4(point, 1.0f);
      clipSpace /= clipSpace.w;
      return ImVec2{(clipSpace.x + 1.0f) * 0.5f * ImGui::GetIO().DisplaySize.x,
                    (clipSpace.y + 1.0f) * 0.5f * ImGui::GetIO().DisplaySize.y};
    };

    for (const auto &line : g_debugLines) {
      glm::vec4 viewSpaceA = view * glm::vec4(line.a, 1.0f);
      glm::vec4 viewSpaceB = view * glm::vec4(line.b, 1.0f);

      // Near plane distance (typically small positive value like 0.01)
      const float nearPlane = 0.01f;

      // If both points are behind camera, skip
      if (viewSpaceA.z > -nearPlane && viewSpaceB.z > -nearPlane) { continue; }

      glm::vec3 startPoint = line.a;
      glm::vec3 endPoint = line.b;

      // Clip line to near plane if one point is behind camera
      if (viewSpaceA.z > -nearPlane || viewSpaceB.z > -nearPlane) {
        glm::vec3 worldA = line.a;
        glm::vec3 worldB = line.b;

        // Calculate intersection with near plane in view space
        float t = (-nearPlane - viewSpaceA.z) / (viewSpaceB.z - viewSpaceA.z);
        t = glm::clamp(t, 0.0f, 1.0f);

        if (viewSpaceA.z > -nearPlane) {
          startPoint = worldA + t * (worldB - worldA);
        } else if (viewSpaceB.z > -nearPlane) {
          endPoint = worldA + t * (worldB - worldA);
        }
      }

      auto start = projectToScreen(startPoint);
      auto end = projectToScreen(endPoint);
      draw_list->AddLine(start, end,
                         IM_COL32(line.color.r * 255, line.color.g * 255, line.color.b * 255, 255),
                         line.thickness);
    }

    // Clear the debug lines after rendering
    g_debugLines.clear();
  }



  REN_PLUGIN("Debug Draw", [](ren::Application &app) {
    app.onEvent<DebugDrawEvent>([](const DebugDrawEvent &e) { renderDebugLines(e.view, e.proj); });
  });

  void DebugScribe::drawCube(glm::vec3 center, float size, glm::vec3 color, float thickness) {
    float halfSize = size / 2.0f;
    glm::vec3 vertices[8] = {center + glm::vec3(-halfSize, -halfSize, -halfSize),
                             center + glm::vec3(halfSize, -halfSize, -halfSize),
                             center + glm::vec3(halfSize, halfSize, -halfSize),
                             center + glm::vec3(-halfSize, halfSize, -halfSize),
                             center + glm::vec3(-halfSize, -halfSize, halfSize),
                             center + glm::vec3(halfSize, -halfSize, halfSize),
                             center + glm::vec3(halfSize, halfSize, halfSize),
                             center + glm::vec3(-halfSize, halfSize, halfSize)};

    int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                        {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};

    for (const auto &edge : edges) {
      debugLineLocal(localToWorld, vertices[edge[0]], vertices[edge[1]], color, thickness);
    }
  }

  void DebugScribe::drawSphere(glm::vec3 center, float radius, glm::vec3 color, float thickness) {
    const int segments = 32;
    for (int i = 0; i < segments; ++i) {
      float theta1 = glm::two_pi<float>() * i / segments;
      float theta2 = glm::two_pi<float>() * (i + 1) / segments;

      glm::vec3 p1 = center + radius * glm::vec3(cos(theta1), sin(theta1), 0);
      glm::vec3 p2 = center + radius * glm::vec3(cos(theta2), sin(theta2), 0);
      glm::vec3 p3 = center + radius * glm::vec3(0, cos(theta1), sin(theta1));
      glm::vec3 p4 = center + radius * glm::vec3(0, cos(theta2), sin(theta2));
      glm::vec3 p5 = center + radius * glm::vec3(cos(theta1), 0, sin(theta1));
      glm::vec3 p6 = center + radius * glm::vec3(cos(theta2), 0, sin(theta2));

      debugLineLocal(localToWorld, p1, p2, color, thickness);
      debugLineLocal(localToWorld, p3, p4, color, thickness);
      debugLineLocal(localToWorld, p5, p6, color, thickness);
    }
  }

  void DebugScribe::drawAxes(float length, float thickness) {
    debugLineLocal(localToWorld, glm::vec3(0, 0, 0), glm::vec3(length, 0, 0), glm::vec3(1, 0, 0),
                   thickness);
    debugLineLocal(localToWorld, glm::vec3(0, 0, 0), glm::vec3(0, length, 0), glm::vec3(0, 1, 0),
                   thickness);
    debugLineLocal(localToWorld, glm::vec3(0, 0, 0), glm::vec3(0, 0, length), glm::vec3(0, 0, 1),
                   thickness);
  }

  void DebugScribe::drawArrow(glm::vec3 start, glm::vec3 end, glm::vec3 color, float thickness) {
    // Draw the main line of the arrow
    debugLineLocal(localToWorld, start, end, color, thickness);

    // Calculate the direction and length of the arrow
    glm::vec3 direction = glm::normalize(end - start);
    float arrowHeadLength =
        glm::length(end - start) * 0.2f;  // Arrowhead size relative to arrow length

    // Calculate the orthogonal vectors for the arrowhead
    glm::vec3 up = glm::vec3(0, 1, 0);
    if (glm::abs(glm::dot(direction, up)) > 0.99f) { up = glm::vec3(1, 0, 0); }
    glm::vec3 right = glm::normalize(glm::cross(direction, up));
    up = glm::normalize(glm::cross(right, direction));

    // Draw the arrowhead
    glm::vec3 arrowTip1 = end - direction * arrowHeadLength + right * arrowHeadLength * 0.5f;
    glm::vec3 arrowTip2 = end - direction * arrowHeadLength - right * arrowHeadLength * 0.5f;
    glm::vec3 arrowTip3 = end - direction * arrowHeadLength + up * arrowHeadLength * 0.5f;
    glm::vec3 arrowTip4 = end - direction * arrowHeadLength - up * arrowHeadLength * 0.5f;

    debugLineLocal(localToWorld, end, arrowTip1, color, thickness);
    debugLineLocal(localToWorld, end, arrowTip2, color, thickness);
    debugLineLocal(localToWorld, end, arrowTip3, color, thickness);
    debugLineLocal(localToWorld, end, arrowTip4, color, thickness);
  }

  void DebugScribe::line(glm::vec3 start, glm::vec3 end, glm::vec3 color, float thickness) {
    debugLineLocal(localToWorld, start, end, color, thickness);
  }

}  // namespace ren


// LUA FFI
extern "C" void __lua_draw_debug_line(glm::vec3 a, glm::vec3 b, glm::vec3 color, float thickness) {
  ren::debugLine(a, b, color, thickness);
}