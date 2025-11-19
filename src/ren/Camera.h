#pragma once
#include <ren/types.h>
#include <ren/core/Instrumentation.h>
#include <SDL2/SDL.h>

namespace ren {
  struct Camera {
    static constexpr float FOV = 90.0f;
    static constexpr float NEAR_PLANE = 0.01f;
    static constexpr float FAR_PLANE = 100.0f;

    glm::vec3 velocity = {0.0f, 0.0f, 0.0f};
    glm::vec3 position = {0.0f, 0.0f, 3.0f};
    glm::vec3 angles = {0.0f, 0.0f, 0.0f};  // pitch, yaw, roll
    bool mouse_captured = false;
    bool first_update = true;
    float cameraSpeed = 32.0f;

    glm::mat4 projection;  // HACK: REMOVE ME

    SDL_GameController *controller = nullptr;


    Camera(void);

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

    static Camera &get(void);  // TODO: remove me!


    inline static glm::mat4 projectionMatrix(float renderWidth, float renderHeight) {
      float renderAspect = renderWidth / renderHeight;
      auto projection = glm::perspective(glm::radians(ren::Camera::FOV), renderAspect,
                                         ren::Camera::NEAR_PLANE, ren::Camera::FAR_PLANE);
      projection[1][1] *= -1;  // Vulkan thing.
      return projection;
    }


    inline glm::mat4 getTemporarySunShadowMapMatrix(glm::vec3 sunDir,
                                                    float orthoSize = 20.0f) const {
      // glm::vec3 sunDir = glm::normalize(glm::vec3(0.0f, 1.0f, 1.0f));
      glm::vec3 sunPos = position - glm::normalize(sunDir) * 20.0f;
      glm::mat4 lightView = glm::lookAt(sunPos, position, glm::vec3(0.0f, 1.0f, 0.0f));
      glm::mat4 lightProj = glm::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, 1.0f, 100.0f);
      auto M = lightProj * lightView;

      // Vulkan NDC correction (flip Y)
      M[1][1] *= -1;
      return M;
    }
  };

}  // namespace ren
