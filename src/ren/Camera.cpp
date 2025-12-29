
#include <imgui/imgui.h>
#include <ren/Camera.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_mouse.h>
#include <ren/core/Instrumentation.h>


static ren::Camera *g_camera; // TODO: NUKE ME

ren::Camera::Camera(void) {
  g_camera = this;
}

ren::Camera &ren::Camera::get(void) {
  return *g_camera;
}

void ren::Camera::update(float dt) {
  REN_PROFILE_FUNCTION();

  const auto* keys = SDL_GetKeyboardState(NULL);
  float mouse_x, mouse_y;
  u32 mouse = SDL_GetMouseState(NULL, NULL);
  SDL_GetRelativeMouseState(&mouse_x, &mouse_y);

  auto& io = ImGui::GetIO();

  bool right_pressed = mouse & SDL_BUTTON_MASK(SDL_BUTTON_RIGHT);

  if (right_pressed && !mouse_captured && not io.WantCaptureMouse) {
    // SDL_SetRelativeMouseMode(SDL_TRUE);
    printf("Mouse captured, relative mode enabled\n");
    mouse_captured = true;
  } else if (!right_pressed && mouse_captured) {
    // SDL_SetRelativeMouseMode(SDL_FALSE);
    printf("Mouse released, capturing disabled\n");
    mouse_captured = false;
  }



  Sint16 left_x = SDL_GetGamepadAxis(controller, SDL_GAMEPAD_AXIS_LEFTX);
  Sint16 left_y = SDL_GetGamepadAxis(controller, SDL_GAMEPAD_AXIS_LEFTY);
  Sint16 right_x = SDL_GetGamepadAxis(controller, SDL_GAMEPAD_AXIS_RIGHTX);
  Sint16 right_y = SDL_GetGamepadAxis(controller, SDL_GAMEPAD_AXIS_RIGHTY);

  glm::vec3 left_stick(0.0f);
  glm::vec3 right_stick(0.0f);

  // Deadzone threshold for analog sticks
  const Sint16 deadzone = 5000;  // Adjust as needed

  if (abs(left_x) > deadzone || abs(left_y) > deadzone) {
    left_stick.x = static_cast<float>(left_x) / SDL_JOYSTICK_AXIS_MAX;
    left_stick.y = static_cast<float>(left_y) / SDL_JOYSTICK_AXIS_MAX;
  }

  if (abs(right_x) > deadzone || abs(right_y) > deadzone) {
    right_stick.x = static_cast<float>(right_x) / SDL_JOYSTICK_AXIS_MAX;
    right_stick.y = static_cast<float>(right_y) / SDL_JOYSTICK_AXIS_MAX;
  }
  // update camera angles based on controller input unconditionally
  {
    float sensitivity = 3.0f;
    angles.y += right_stick.x * sensitivity * dt;  // yaw
    angles.x -= right_stick.y * sensitivity * dt;  // pitch



    // update impulse based on controller input
    float speed = cameraSpeed * dt;

    glm::vec3 impulse(0.0f);

    left_stick.y = -left_stick.y;  // Invert Y-axis for forward/backward movement

    // impulse.x += speed * left_stick.y;  // Forward
    // impulse.z += speed * left_stick.y;

    // impulse.x -= speed * -left_stick.y;  // Backward
    // impulse.z -= speed * -left_stick.y;


    // Movement vectors
    float cos_yaw = cosf(angles.y);
    float sin_yaw = sinf(angles.y);
    glm::vec3 forward = {sin_yaw, 0.0f, -cos_yaw};
    glm::vec3 right = {cos_yaw, 0.0f, sin_yaw};

    impulse.x += right.x * speed * left_stick.x;
    impulse.z += right.z * speed * left_stick.x;

    impulse.x += forward.x * speed * left_stick.y;
    impulse.z += forward.z * speed * left_stick.y;



    // // move up if the user is pressing the x button
    // if (SDL_GetGamepadButton(controller, SDL_GAMEPAD_BUTTON_BACK)) { impulse.y += speed; }

    // // move down if the user is pressing the circle button
    // if (SDL_GetGamepadButton(controller, SDL_GAMEPAD_BUTTON_)) { impulse.y -= speed; }



    velocity += impulse;
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