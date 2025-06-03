#include <SDL2/SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include <ren/RenderTarget.h>
#include <ren/math.h>
#include <ren/screen.h>

#define WINDOW_WIDTH (320 * 2)
#define WINDOW_HEIGHT (240 * 2)
#define WINDOW_SCALE 2

// #define WINDOW_WIDTH 1920
// #define WINDOW_HEIGHT 1080
// #define WINDOW_SCALE 1

using namespace ren;

void render(RenderTarget &targ, int frame) {
  (void)frame;
  targ.clear(float3(0.0));

  int mouse_x, mouse_y;
  SDL_GetMouseState(&mouse_x, &mouse_y);
  mouse_x = (int)(mouse_x / (float)WINDOW_SCALE);
  mouse_y = (int)(mouse_y / (float)WINDOW_SCALE);

  auto a = float2(0.2f * targ.width, 0.2f * targ.height);
  auto b = float2(0.25f * targ.width, 0.8f * targ.height);

  auto c = float2(mouse_x, mouse_y);

  auto d = float2(0.8f * targ.width, 0.8f * targ.height);

  float2 min_xy = float2(std::min({a.x, b.x, c.x}), std::min({a.y, b.y, c.y}));
  float2 max_xy = float2(std::max({a.x, b.x, c.x}), std::max({a.y, b.y, c.y}));
  targ.draw_box(min_xy, max_xy, float3(0.2f, 0.2f, 0.2f));

  float2 center = (a + b + c) / 3.0f;

  targ.rasterize(a, b, c);
  targ.rasterize(c, b, d);

  targ.pix(center.x, center.y) = float3(1.0f, 0.0f, 1.0f);

  // targ.draw_line(a, b, float3(1.0f, 0.0f, 0.0f));
  // targ.draw_line(b, c, float3(0.0f, 1.0f, 0.0f));
  // targ.draw_line(c, a, float3(0.0f, 0.0f, 1.0f));
  
  // targ.draw_box(min_xy, max_xy, float3(1.0f, 1.0f, 1.0f));

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
    // Render a pattern to the pixel buffer.
    render(render_target, frame);

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
    SDL_Delay(16); // ~60 FPS
  }

  // Cleanup
  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}
