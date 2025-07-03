#pragma once
#include <ren/types.h>
#include <ren/core/Instrumentation.h>
#include <SDL2/SDL.h>

namespace ren {
  struct Camera {
    glm::vec3 velocity = {0.0f, 0.0f, 0.0f};
    glm::vec3 position = {0.0f, 0.0f, 3.0f};
    glm::vec3 angles = {0.0f, 0.0f, 0.0f};  // pitch, yaw, roll
    bool mouse_captured = false;
    bool first_update = true;
    float cameraSpeed = 32.0f;

    SDL_GameController *controller = nullptr;


    inline glm::mat4 view_matrix() const {
      REN_PROFILE_FUNCTION();
      float pitch = angles.x;
      float yaw = angles.y;

      float sinPitch = sinf(pitch);
      float cosPitch = cosf(pitch);
      float sinYaw = sinf(yaw);
      float cosYaw = cosf(yaw);

      glm::vec3 forward(sinYaw * cosPitch,  // X component
                        sinPitch,           // Y component
                        -cosYaw * cosPitch  // Z component
      );

      glm::mat4 view = glm::lookAt(position, position + forward, glm::vec3(0.0f, 1.0f, 0.0f));
      return view;
    }

    void update(float dt);
  };

}  // namespace ren