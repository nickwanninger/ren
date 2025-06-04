#include <SDL2/SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include <ren/RenderTarget.h>
#include <ren/math.h>
#include <ren/screen.h>

#include <string>
#include <vector>

// #include <wren.hpp>

#define WINDOW_WIDTH (320 * 2)
#define WINDOW_HEIGHT (240 * 2)
#define WINDOW_SCALE 2

// #define WINDOW_WIDTH 1920
// #define WINDOW_HEIGHT 1080
// #define WINDOW_SCALE 1

using namespace ren;

struct Triangle {
  float3 p[3];
};

struct Mesh {
  std::vector<Triangle> triangles;

  void add_triangle(const float3 &a, const float3 &b, const float3 &c) {
    triangles.push_back({{a, b, c}});
  }
};

void render(RenderTarget &targ, int frame) {

  int mouse_x, mouse_y;
  SDL_GetMouseState(&mouse_x, &mouse_y);
  mouse_x = (int)(mouse_x / (float)WINDOW_SCALE);
  mouse_y = (int)(mouse_y / (float)WINDOW_SCALE);

  float2 mouse_pos = targ.from_device(float2(mouse_x, mouse_y));

  (void)frame;
  targ.clear(float3(0.0));

  float fNear = 0.01f;
  float fFar = 1000.0f;
  float fFov = 90.0f;
  float fAspectRatio = (float)targ.width / (float)targ.height;

  matrix4 matProj = matrix4::perspective(fFov, fAspectRatio, fNear, fFar);
  std::cout << "Projection Matrix:\n" << matProj << std::endl;

  matrix4 matRotZ = matrix4::rotation_x(-mouse_pos.y);
  matrix4 matRotX = matrix4::rotation_y(mouse_pos.x);

  Mesh cube;
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

  for (auto &tri : cube.triangles) {
    float3 a = tri.p[0] - float3(0.5f, 0.5f, 0.5f);
    float3 b = tri.p[1] - float3(0.5f, 0.5f, 0.5f);
    float3 c = tri.p[2] - float3(0.5f, 0.5f, 0.5f);

    a = matRotZ.transform_point(a);
    b = matRotZ.transform_point(b);
    c = matRotZ.transform_point(c);

    a = matRotX.transform_point(a);
    b = matRotX.transform_point(b);
    c = matRotX.transform_point(c);

    a.z -= 2;
    b.z -= 2;
    c.z -= 2;

    a = matProj.transform_point(a);
    b = matProj.transform_point(b);
    c = matProj.transform_point(c);

    // targ.rasterize(c.xy(), b.xy(), a.xy());

    std::cout << "Triangle: " << a << " " << b << " " << c << std::endl;
    targ.draw_line(a.xy(), b.xy(), float3(1, 1, 1));
    targ.draw_line(b.xy(), c.xy(), float3(1, 1, 1));
    targ.draw_line(c.xy(), a.xy(), float3(1, 1, 1));
  }

  // auto a = float2(-0.5, -0.5);
  // auto b = float2(0.0, 0.1);
  // auto c = mouse_pos;
  // targ.rasterize(a, b, c);

  // targ.draw_line(a, b, float3(1.0f, 0.0f, 0.0f));
  // targ.draw_line(b, c, float3(0.0f, 1.0f, 0.0f));
  // targ.draw_line(c, a, float3(0.0f, 0.0f, 1.0f));

  // targ.draw_box(min_xy, max_xy, float3(1.0f, 1.0f, 1.0f));
  targ.draw_line(float2(0.0f, 0.0f), mouse_pos, float3(1.0f, 1.0f, 1.0f));
}

int main(void) {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
    return 1;
  }

  SDL_Window *window =
      SDL_CreateWindow("ren", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                       WINDOW_WIDTH * WINDOW_SCALE,
                       WINDOW_HEIGHT * WINDOW_SCALE, SDL_WINDOW_SHOWN);

  if (!window) {
    printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  SDL_Renderer *renderer =
      SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if (!renderer) {
    printf("Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  // Create a streaming texture for CPU-side pixel manipulation
  SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                           SDL_TEXTUREACCESS_STREAMING,
                                           WINDOW_WIDTH, WINDOW_HEIGHT);

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

  while (running) {
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) {
        running = 0;
      }
    }

    // Lock texture for CPU writing
    void *bitmap;
    int pitch;
    if (SDL_LockTexture(texture, NULL, &bitmap, &pitch) < 0) {
      printf("Unable to lock texture! SDL_Error: %s\n", SDL_GetError());
      break;
    }

    uint64_t framestart = SDL_GetPerformanceCounter();

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
    // SDL_Delay(16); // ~60 FPS
  }

  // Cleanup
  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}
