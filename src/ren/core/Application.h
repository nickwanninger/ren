#pragma once

#include <ren/renderer/Vulkan.h>
#include <ren/layers/LayerStack.h>
#include <SDL2/SDL.h>
#include <ren/renderer/RenderPass.h>
#include <ren/renderer/Renderer.h>
#include <ren/core/Scene.h>
#include <ren/core/Entity.h>
#include <ren/layers/SceneLayer.h>
#include <ren/layers/ImGuiLayer.h>

namespace ren {



  class Application {
    // The first important thing in an application is the SDL Window and the Vulkan instance.
    SDL_Window *window = nullptr;
    ref<Renderer> renderer;

    ren::LayerStack layerStack;

    bool running = true;

    ref<SceneLayer> sceneLayer = nullptr;
    ref<ImGuiLayer> imguiLayer = nullptr;

   public:
    Application(const std::string &app_name, glm::uvec2 window_size);
    ~Application();

    void run();


    static Application &get(void);

    SDL_Window *getWindow(void) const { return this->window; }


   private:
  };
}  // namespace ren