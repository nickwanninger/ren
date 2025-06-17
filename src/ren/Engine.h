#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include <ren/renderer/Vulkan.h>
#include <string>

namespace ren {
  // In ren, we have only one kind of rendering engine, and that
  // is a 3D engine based on Vulkan and SDL. In the future, we
  // might investigate a non-graphical (headless) engine for
  // server stuff but I'm not worried about that yet.
  // When you use ren, you extend this class for your own class.
  class Engine {
   public:
    Engine(const std::string &app_name, glm::uvec2 window_size);

    void run();

   private:
    std::string app_name;
    // The Engine has a pointer to the SDL window for getting input and whatnot.
    SDL_Window *window = nullptr;

    // It also has a pointer to the Vulkan instance at the top
    std::unique_ptr<ren::VulkanInstance> vulkan;
  };
};  // namespace ren
