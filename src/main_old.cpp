#include <SDL2/SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include <ren/RenderTarget.h>
#include <ren/math.h>
#include <ren/geom.h>

#include <string>
#include <vector>


#include <teapot.h>


#include <GLFW/glfw3.h>



// #define WINDOW_WIDTH 50
// #define WINDOW_HEIGHT 50
// #define WINDOW_SCALE 6

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

void render(RenderTarget &targ, int frame) {
  int mouse_x, mouse_y;
  SDL_GetMouseState(&mouse_x, &mouse_y);

  (void)frame;

  // clear to sky blue
  // targ.clear(glm::vec3(0.5f, 0.7f, 1.0f));
  targ.clear(glm::vec3(0.0f));

  float fNear = 0.5f;
  float fFar = 100.0f;
  float wScale = 1.0f;
  float fFov = 90.0f / wScale;
  float fAspectRatio = (float)targ.width / (float)targ.height;




  glm::mat4 view = camera.view_matrix();
  glm::mat4 proj = glm::perspective(glm::radians(fFov), fAspectRatio, fNear, fFar);

  glm::mat4 vp = proj * view;

  Mesh cube;

  // for (int i = 0; i < teapot_count; i += 9) {
  //   Triangle face = *(Triangle *)(teapot + i);
  //   face.p[0].y *= -1;  // Invert Y for OpenGL style
  //   face.p[1].y *= -1;  // Invert Y for OpenGL style
  //   face.p[2].y *= -1;  // Invert Y for OpenGL style
  //   // This thing is wound wrong, I think.
  //   cube.add_triangle(face.p[2], face.p[1], face.p[0]);
  // }

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


  // int count = 1;
  // for (int x = 0; x < count; x++) {
  //   for (int y = 0; y < count; y++) {
  //     glm::vec3 pos = glm::vec3(x * 2.0f, 0.0f, y * 2.0f);
  //     for (auto &tri : cube.triangles) {
  //       glm::vec4 a = glm::vec4(tri.a, 1);
  //       glm::vec4 b = glm::vec4(tri.b, 1);
  //       glm::vec4 c = glm::vec4(tri.c, 1);

  //       auto model = glm::translate(glm::mat4(1.0f), pos);
  //       auto mvp = proj * view * model;

  //       a = mvp * a;
  //       b = mvp * b;
  //       c = mvp * c;

  //       targ.rasterizeClip(a, b, c);
  //     }
  //   }
  // }


  // Draw a triangle in the center of the screen




  glm::vec4 a = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
  glm::vec4 b = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
  glm::vec4 c = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);

  ren::Triangle tri({ren::Vertex(a), ren::Vertex(b), ren::Vertex(c)});
  tri.compute_normals();

  printf("sizeof triangle: %zu\n", sizeof(tri));

  // clockwise
  targ.rasterizeClip(vp * a, vp * c, vp * b);


  for (auto &v : tri.v) {
    auto norm = glm::vec4(v.normal, 0.0f);
    auto start = vp * v.position;
    auto end = vp * (v.position + norm);


    std::cout << v.position << " -> " << (v.position + norm) << std::endl;


    targ.draw_line(start / start.w, end / end.w, v.color);
  }




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

    // float zBuffer = (end.z + 1.0f) * 0.5f;

    targ.draw_line(start, end, color);
    targ.draw_circle(end, 0.02f, circle_color);
  };

  // Positive x is red
  render_direction(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), "x");
  // Positive y is green
  render_direction(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), "y");
  // Positive z is blue
  render_direction(glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 0.0f, 1.0f), "z");

  // crosshair thing
  targ.draw_circle(glm::vec2(0.0f, 0.0f), 0.01f, glm::vec3(1.0f, 1.0f, 1.0f));
}

int main(void) {
  // return new_main();
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

    render_target.triangles = 0;  // Reset triangle count for this frame
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
    printf("Frame: %7d rendered %lu triangles in %fms\n", frame, render_target.triangles, seconds * 1000);
    SDL_Delay(16);  // ~60 FPS
  }

  // Cleanup
  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}
