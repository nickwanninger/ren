

#include <ren/Camera.h>
#include <SDL2/SDL.h>

void ren::Camera::update(float dt) {
  const Uint8* keys = SDL_GetKeyboardState(NULL);
  int mouse_x, mouse_y;
  SDL_GetRelativeMouseState(&mouse_x, &mouse_y);

  if (first_update) {
    // Capture mouse on first update
    SDL_SetRelativeMouseMode(mouse_captured ? SDL_TRUE : SDL_FALSE);
    first_update = false;
  }

  // Toggle mouse capture with ESC
  if (keys[SDL_SCANCODE_E]) {
    mouse_captured = !mouse_captured;
    SDL_SetRelativeMouseMode(mouse_captured ? SDL_TRUE : SDL_FALSE);
    SDL_Delay(100);  // Prevent rapid toggling
  }




  if (!mouse_captured) return;

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
  if (glm::length(velocity) > cameraSpeed) {
    // clamp velocity to cameraSpeed
    velocity = glm::normalize(velocity) * cameraSpeed;
  }

  // Apply velocity to position
  position += velocity * dt;
  // damp velocity
  velocity *= 0.95f;  // Dampen the velocity to simulate friction
}