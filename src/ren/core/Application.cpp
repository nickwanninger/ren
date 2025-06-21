
#include <ren/core/Application.h>



namespace ren {
  Application::Application(const std::string &app_name, glm::uvec2 window_size) {
    // Initialize the SDL window
    SDL_Init(SDL_INIT_VIDEO);
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    this->window =
        SDL_CreateWindow(app_name.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                         window_size.x, window_size.y, window_flags);

    // Create the Vulkan instance
    this->vulkan = makeRef<VulkanInstance>(app_name, this->window);
  }

  Application::~Application() {
    // Clear the layer stack
    this->layerStack.clear();

    // Then destroy vulkan.
    this->vulkan.reset();

    // And finally, close the SDL window
    SDL_DestroyWindow(this->window);
    this->window = nullptr;
  }


  void Application::run() {
    while (this->running) {
      //
    }
  }
}  // namespace ren