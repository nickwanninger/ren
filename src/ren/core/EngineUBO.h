#pragma once
#include <ren/types.h>


namespace ren {

  struct EngineUBO {
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 invViewProj;  // inverse(proj * view)

    // These must be vec4 becasue of std140 alignment.
    glm::vec4 cameraWorldPosition;

    glm::vec4 lightDirection = glm::vec4(0.0, 1.0, 1.0, 1.0);  // TEMP
    float time;
  };
}  // namespace ren