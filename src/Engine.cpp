#include <ren/Engine.h>

ren::Engine::Engine(const std::string &app_name, glm::uvec2 window_size)
    : app_name(app_name) {

  SDL_Init(SDL_INIT_VIDEO);
  SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);

  this->window = SDL_CreateWindow(app_name.c_str(), SDL_WINDOWPOS_UNDEFINED,
                                  SDL_WINDOWPOS_UNDEFINED, window_size.x,
                                  window_size.y, window_flags);
}

void ren::Engine::run(void) {

  SDL_Event e;
  bool bQuit = false;

  // main loop
  while (!bQuit) {
    // Handle events on queue
    while (SDL_PollEvent(&e) != 0) {
      // close the window when user alt-f4s or clicks the X button
      if (e.type == SDL_QUIT) {
        bQuit = true;
      } else if (e.type == SDL_KEYDOWN) {
        if (e.key.keysym.sym == SDLK_SPACE) {
        }
      }
    }

    // draw();
  }
}
