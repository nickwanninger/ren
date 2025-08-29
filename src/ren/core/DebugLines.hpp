#pragma once

#include <ren/types.h>
#include <ren/renderer/Renderer.h>


namespace ren {


  // This is dispatched from the renderer
  struct DebugDrawEvent {
    glm::mat4 view;
    glm::mat4 proj;
  };


  // Draw an immediate line from point a to b, with a color.
  void debugLine(glm::vec3 a, glm::vec3 b, glm::vec3 color = glm::vec3(1.0, 0.0, 0.0),
                 float thickness = 1.0f);
  void debugLineLocal(const glm::mat4 &localToWorld, glm::vec3 a, glm::vec3 b,
                      glm::vec3 color = glm::vec3(1.0, 0.0, 0.0), float thickness = 1.0f);

  void debugRay(glm::vec3 origin, glm::vec3 direction, glm::vec3 color = glm::vec3(1.0, 0.0, 0.0),
                float thickness = 1.0f);

  // Actually dispatch the debug lines (Drawn using ImGui in the background (on top of the scene))
  void renderDebugLines(glm::mat4 view, glm::mat4 proj);


  class DebugScribe {
    glm::mat4 localToWorld;

   public:
    DebugScribe()
        : localToWorld(glm::mat4(1.0f)) {}
    explicit DebugScribe(const glm::mat4 &transform)
        : localToWorld(transform) {}


    void line(glm::vec3 start, glm::vec3 end, glm::vec3 color = glm::vec3(1.0, 0.0, 0.0),
              float thickness = 1.0f);
    void drawCube(glm::vec3 center, float size, glm::vec3 color = glm::vec3(1.0, 0.0, 0.0),
                  float thickness = 1.0f);
    void drawSphere(glm::vec3 center, float radius, glm::vec3 color = glm::vec3(1.0, 0.0, 0.0),
                    float thickness = 1.0f);
    void drawAxes(float length = 1.0f, float thickness = 1.0f);
    void drawArrow(glm::vec3 start, glm::vec3 end, glm::vec3 color = glm::vec3(1.0, 0.0, 0.0),
                   float thickness = 1.0f);
  };

}  // namespace ren