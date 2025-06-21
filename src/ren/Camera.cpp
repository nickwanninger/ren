
#include <imgui/imgui.h>
#include <ren/Camera.h>
#include <SDL2/SDL.h>
#include <ren/core/Instrumentation.h>

void ren::Camera::update(float dt) {
  REN_PROFILE_FUNCTION();

  const Uint8* keys = SDL_GetKeyboardState(NULL);
  int mouse_x, mouse_y;
  u32 mouse = SDL_GetMouseState(NULL, NULL);
  SDL_GetRelativeMouseState(&mouse_x, &mouse_y);

  auto& io = ImGui::GetIO();

  bool right_pressed = mouse & SDL_BUTTON(SDL_BUTTON_RIGHT);

  if (right_pressed && !mouse_captured && not io.WantCaptureMouse) {
    SDL_SetRelativeMouseMode(SDL_TRUE);
    printf("Mouse captured, relative mode enabled\n");
    mouse_captured = true;
  } else if (!right_pressed && mouse_captured) {
    SDL_SetRelativeMouseMode(SDL_FALSE);
    printf("Mouse released, capturing disabled\n");
    mouse_captured = false;
  }



  if (mouse_captured and !io.WantCaptureKeyboard) {
    // Mouse look
    angles.y += mouse_x * 0.002f;  // yaw
    angles.x -= mouse_y * 0.002f;  // pitch

    // Clamp pitch
    if (angles.x > M_PI / 2.0f) angles.x = M_PI / 2.0f;
    if (angles.x < -M_PI / 2.0f) angles.x = -M_PI / 2.0f;

    // Movement vectors
    float cos_yaw = cosf(angles.y);
    float sin_yaw = sinf(angles.y);
    glm::vec3 forward = {sin_yaw, 0.0f, -cos_yaw};
    glm::vec3 right = {cos_yaw, 0.0f, sin_yaw};

    float speed = cameraSpeed * dt;

    glm::vec3 impulse(0.0f);

    // WASD movement
    if (keys[SDL_SCANCODE_W]) {
      // impulse.x += speed / 2.0f;
      impulse.x += forward.x * speed;
      impulse.z += forward.z * speed;
    }
    if (keys[SDL_SCANCODE_S]) {
      // impulse.x -= speed / 2.0f;
      impulse.x -= forward.x * speed;
      impulse.z -= forward.z * speed;
    }
    if (keys[SDL_SCANCODE_A]) {
      // impulse.z -= speed / 2.0f;
      impulse.x -= right.x * speed;
      impulse.z -= right.z * speed;
    }
    if (keys[SDL_SCANCODE_D]) {
      // impulse.z += speed / 2.0f;
      impulse.x += right.x * speed;
      impulse.z += right.z * speed;
    }
    if (keys[SDL_SCANCODE_SPACE]) { impulse.y += speed; }
    if (keys[SDL_SCANCODE_LSHIFT]) { impulse.y -= speed; }
    velocity += impulse;
  }

  if (glm::length(velocity) > cameraSpeed) {
    // clamp velocity to cameraSpeed
    velocity = glm::normalize(velocity) * cameraSpeed;
  }

  // Apply velocity to position
  position += velocity * dt;
  // damp velocity according to dt
  velocity *= (1.0f - dt * 5.0f);  // Damping factor, adjust as needed
}