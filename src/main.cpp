#include <SDL2/SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include <ren/RenderTarget.h>
#include <ren/math.h>
#include <ren/screen.h>

#include <string>
#include <vector>


#include <teapot.h>

#define WINDOW_WIDTH (320 * 1)
#define WINDOW_HEIGHT (240 * 1)
#define WINDOW_SCALE 3

// #define WINDOW_WIDTH 1920
// #define WINDOW_HEIGHT 1080
// #define WINDOW_SCALE 1

using namespace ren;

struct Camera {
  glm::vec3 position = {0.5f, 1.0f, 2.0f};
  glm::vec3 angles = {0.0f, 0.0f, 0.0f};  // pitch, yaw, roll
  bool mouse_captured = true;

  glm::mat4 view_matrix() const {
    float pitch = angles.x;
    float yaw = angles.y;

    float sinPitch = sinf(pitch);
    float cosPitch = cosf(pitch);
    float sinYaw = sinf(yaw);
    float cosYaw = cosf(yaw);

    glm::vec3 forward(sinYaw * cosPitch,  // X component
        sinPitch,                         // Y component
        -cosYaw * cosPitch                // Z component
    );

    glm::mat4 view = glm::lookAt(position, position + forward, glm::vec3(0.0f, 1.0f, 0.0f));
    return view;
  }

  void update(float dt) {
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    int mouse_x, mouse_y;
    SDL_GetRelativeMouseState(&mouse_x, &mouse_y);

    // Toggle mouse capture with ESC
    if (keys[SDL_SCANCODE_ESCAPE]) {
      mouse_captured = !mouse_captured;
      SDL_SetRelativeMouseMode(mouse_captured ? SDL_TRUE : SDL_FALSE);
      SDL_Delay(100);  // Prevent rapid toggling
    }

    // Mouse look
    if (mouse_captured) {
      angles.y += mouse_x * 0.002f;  // yaw
      angles.x -= mouse_y * 0.002f;  // pitch

      // Clamp pitch
      if (angles.x > M_PI / 2.0f) angles.x = M_PI / 2.0f;
      if (angles.x < -M_PI / 2.0f) angles.x = -M_PI / 2.0f;
    }

    // Movement vectors
    float cos_yaw = cosf(angles.y);
    float sin_yaw = sinf(angles.y);
    glm::vec3 forward = {sin_yaw, 0.0f, -cos_yaw};
    glm::vec3 right = {cos_yaw, 0.0f, sin_yaw};

    float speed = 1.0f * dt;

    // WASD movement
    if (keys[SDL_SCANCODE_W]) {
      // position.x += speed / 2.0f;
      position.x += forward.x * speed;
      position.z += forward.z * speed;
    }
    if (keys[SDL_SCANCODE_S]) {
      // position.x -= speed / 2.0f;
      position.x -= forward.x * speed;
      position.z -= forward.z * speed;
    }
    if (keys[SDL_SCANCODE_A]) {
      // position.z -= speed / 2.0f;
      position.x -= right.x * speed;
      position.z -= right.z * speed;
    }
    if (keys[SDL_SCANCODE_D]) {
      // position.z += speed / 2.0f;
      position.x += right.x * speed;
      position.z += right.z * speed;
    }
    if (keys[SDL_SCANCODE_SPACE]) { position.y += speed; }
    if (keys[SDL_SCANCODE_LSHIFT]) { position.y -= speed; }
  }
};

Camera camera;

struct Triangle {
  glm::vec3 p[3];
};

struct Mesh {
  std::vector<Triangle> triangles;

  void add_triangle(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c) { triangles.push_back({{a, b, c}}); }
};

class NearPlaneClipper {
 private:
  float nearPlane = 0.01f;

  // Check if vertex is in front of near plane (visible)
  bool isVec3Visible(const glm::vec3 &v) const {
    return v.z <= -nearPlane;  // Negative Z is forward in view space
  }

  // Get signed distance from vertex to near plane
  float getDistance(const glm::vec3 &v) const { return v.z + nearPlane; }




  // Compute intersection point between edge and near plane
  glm::vec3 computeIntersection(const glm::vec3 &v1, const glm::vec3 &v2) const {
    float d1 = getDistance(v1);
    float d2 = getDistance(v2);

    // Parametric intersection: t = d1 / (d1 - d2)
    float t = d1 / (d1 - d2);

    return ren::lerp(v1, v2, t);
  }

 public:
  NearPlaneClipper(float nearPlane)
      : nearPlane(nearPlane) {}

  // Clip a single triangle against the near plane
  // Returns number of output triangles (0, 1, or 2)
  int clipTriangle(const glm::vec3 &v0, const glm::vec3 &v1, const glm::vec3 &v2, glm::vec3 *outTriangles) const {
    std::vector<glm::vec3> inputList = {v0, v1, v2};
    std::vector<glm::vec3> outputList;

    // Sutherland-Hodgman clipping against near plane
    if (inputList.empty()) return 0;

    glm::vec3 prevVec3 = inputList.back();
    bool prevVisible = isVec3Visible(prevVec3);

    for (const glm::vec3 &currentVec3 : inputList) {
      bool currentVisible = isVec3Visible(currentVec3);

      if (currentVisible) {
        if (!prevVisible) {
          // Entering visible region - add intersection
          outputList.push_back(computeIntersection(prevVec3, currentVec3));
        }
        // Add current vertex
        outputList.push_back(currentVec3);
      } else if (prevVisible) {
        // Exiting visible region - add intersection
        outputList.push_back(computeIntersection(prevVec3, currentVec3));
      }
      // If both invisible, add nothing

      prevVec3 = currentVec3;
      prevVisible = currentVisible;
    }

    // Convert clipped polygon back to triangles
    if (outputList.size() < 3) {
      return 0;  // Completely clipped
    } else if (outputList.size() == 3) {
      // Still a triangle
      outTriangles[0] = outputList[0];
      outTriangles[1] = outputList[1];
      outTriangles[2] = outputList[2];
      return 1;
    } else if (outputList.size() == 4) {
      // Quad - split into two triangles
      // Triangle 1: 0,1,2
      outTriangles[0] = outputList[0];
      outTriangles[1] = outputList[1];
      outTriangles[2] = outputList[2];

      // Triangle 2: 0,2,3
      outTriangles[3] = outputList[0];
      outTriangles[4] = outputList[2];
      outTriangles[5] = outputList[3];
      return 2;
    }

    // For polygons with >4 vertices, use fan triangulation
    int numTriangles = outputList.size() - 2;
    for (int i = 0; i < numTriangles; ++i) {
      outTriangles[i * 3 + 0] = outputList[0];
      outTriangles[i * 3 + 1] = outputList[i + 1];
      outTriangles[i * 3 + 2] = outputList[i + 2];
    }
    return numTriangles;
  }

  // Convenience method for simple culling check
  bool shouldCullTriangle(const glm::vec3 &v0, const glm::vec3 &v1, const glm::vec3 &v2) const {
    return !isVec3Visible(v0) && !isVec3Visible(v1) && !isVec3Visible(v2);
  }
};

void render(RenderTarget &targ, int frame) {
  int mouse_x, mouse_y;
  SDL_GetMouseState(&mouse_x, &mouse_y);

  glm::vec2 mouse_pos = targ.from_device(glm::vec2(mouse_x / (float)WINDOW_SCALE, mouse_y / (float)WINDOW_SCALE));

  (void)frame;

  // clear to sky blue
  targ.clear(glm::vec3(0.5f, 0.7f, 1.0f));

  float fNear = 0.1f;
  float fFar = 1000.0f;
  float wScale = 1.0f;
  float fFov = 90.0f / wScale;
  float fAspectRatio = (float)targ.width / (float)targ.height;




  NearPlaneClipper clipper(fNear);

  glm::mat4 view = camera.view_matrix();
  glm::mat4 proj = glm::perspective(glm::radians(fFov), fAspectRatio, fNear, fFar);

  Mesh cube;

  for (int i = 0; i < teapot_count; i += 9) {
    Triangle face = *(Triangle *)(teapot + i);
    face.p[0].y *= -1;  // Invert Y for OpenGL style
    face.p[1].y *= -1;  // Invert Y for OpenGL style
    face.p[2].y *= -1;  // Invert Y for OpenGL style
    // This thing is wound wrong, I think.
    cube.add_triangle(face.p[2], face.p[1], face.p[0]);
  }

#if 0
  cube.add_triangle({0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.0f});
  cube.add_triangle({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f});
  cube.add_triangle({1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f});
  cube.add_triangle({1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f, 1.0f});
  cube.add_triangle({1.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 1.0f});
  cube.add_triangle({1.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f});
  cube.add_triangle({0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f});
  cube.add_triangle({0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f});
  cube.add_triangle({0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f});
  cube.add_triangle({0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 0.0f});
  cube.add_triangle({1.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f});
  cube.add_triangle({1.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f});
#endif


  for (auto &tri : cube.triangles) {
    glm::vec4 a = glm::vec4(tri.p[0], 1);
    glm::vec4 b = glm::vec4(tri.p[1], 1);
    glm::vec4 c = glm::vec4(tri.p[2], 1);

    auto model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f));  // Move model up
    auto mvp = proj * view * model;

    a = mvp * a;
    b = mvp * b;
    c = mvp * c;

    targ.rasterizeClip(a, b, c);
  }


  float floorSize = 10.0f;
  // render a massive floor plane with two triangles
  glm::vec4 floorA = glm::vec4(-floorSize, 0.0f, -floorSize, 1.0f);
  glm::vec4 floorB = glm::vec4(floorSize, 0.0f, -floorSize,  1.0f);
  glm::vec4 floorC = glm::vec4(-floorSize, 0.0f, floorSize,  1.0f);
  glm::vec4 floorD = glm::vec4(floorSize, 0.0f, floorSize,   1.0f);

  auto mvp = proj * view * glm::mat4(3.0f);
  floorA = mvp * floorA;
  floorB = mvp * floorB;
  floorC = mvp * floorC;
  floorD = mvp * floorD;
  targ.rasterizeClip(floorA, floorB, floorC);
  targ.rasterizeClip(floorB, floorC, floorD);



  auto render_direction = [&](glm::vec3 _end, glm::vec3 color, const char *name = "") {
    glm::vec3 circle_color = color;
    glm::vec4 start = {0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 end = glm::vec4(_end, 1.0f);

    start = proj * view * start;
    end = proj * view * end;
    start /= start.w * wScale;




    float clip_w = fabs(end.w);

    if (fabs(end.x) > clip_w || fabs(end.y) > clip_w || fabs(end.z) > clip_w) {
      circle_color = color * 0.5f;  // dim
    }

    end /= end.w * wScale;

    float zBuffer = (end.z + 1.0f) * 0.5f;

    targ.draw_line(start, end, color);
    targ.draw_circle(end, 0.02f, circle_color);
  };

  // Positive x is red
  render_direction(glm::vec3(0.2f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), "x");
  // Positive y is green
  render_direction(glm::vec3(0.0f, 0.2f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), "y");
  // Positive z is blue
  render_direction(glm::vec3(0.0f, 0.0f, 0.2f), glm::vec3(0.0f, 0.0f, 1.0f), "z");

  // crosshair thing
  targ.draw_circle(glm::vec2(0.0f, 0.0f), 0.01f, glm::vec3(1.0f, 1.0f, 1.0f));
}

int main(void) {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
    return 1;
  }

  SDL_Window *window = SDL_CreateWindow("ren", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_CENTERED,
      WINDOW_WIDTH * WINDOW_SCALE, WINDOW_HEIGHT * WINDOW_SCALE, SDL_WINDOW_SHOWN);

  SDL_SetRelativeMouseMode(SDL_TRUE);
  SDL_StopTextInput();
  SDL_EventState(SDL_KEYDOWN, SDL_ENABLE);

  if (!window) {
    printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if (!renderer) {
    printf("Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  // Create a streaming texture for CPU-side pixel manipulation
  SDL_Texture *texture =
      SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, WINDOW_WIDTH, WINDOW_HEIGHT);

  if (!texture) {
    printf("Texture could not be created! SDL_Error: %s\n", SDL_GetError());
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  int running = 1;
  SDL_Event e;
  int frame = 0;
  float perf_freq = (float)SDL_GetPerformanceFrequency();

  RenderTarget render_target(WINDOW_WIDTH, WINDOW_HEIGHT);

  Uint32 last_time = SDL_GetTicks();

  while (running) {
    Uint32 current_time = SDL_GetTicks();
    float dt = (current_time - last_time) / 1000.0f;
    last_time = current_time;

    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) { running = 0; }
    }

    // Lock texture for CPU writing
    void *bitmap;
    int pitch;
    if (SDL_LockTexture(texture, NULL, &bitmap, &pitch) < 0) {
      printf("Unable to lock texture! SDL_Error: %s\n", SDL_GetError());
      break;
    }

    uint64_t framestart = SDL_GetPerformanceCounter();

    camera.update(dt);

    // Render the scene
    render(render_target, frame);
    // Copy pixels to the texture
    render_target.quantize_screen((uint32_t *)bitmap);

    uint64_t frameend = SDL_GetPerformanceCounter();

    // Unlock texture
    SDL_UnlockTexture(texture);

    // Clear screen and copy texture
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
    float seconds = (frameend - framestart) / (float)perf_freq;
    // float fps = 1.0f / seconds;

    frame++;
    printf("Frame: %7d rendered in %fms\n", frame, seconds * 1000);
    SDL_Delay(16);  // ~60 FPS
  }

  // Cleanup
  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}
