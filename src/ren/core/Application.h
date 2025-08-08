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
#include <ren/assets/AssetManager.h>
#include <flecs.h>

namespace ren {



  class Application {
    // The first important thing in an application is the SDL Window and the Vulkan instance.
    SDL_Window *window = nullptr;
    ref<Renderer> renderer;

    ren::LayerStack layerStack;

    bool running = true;

    ref<SceneLayer> sceneLayer = nullptr;
    ref<ImGuiLayer> imguiLayer = nullptr;

    AssetManager &getAssetManager() { return world.get_mut<AssetManager>(); }

   public:
    // Arguably the most important part of the application is the ECS world.
    // We try to put everything in the world, so that we can use flecs systems to process basically
    // everything.
    flecs::world world;
    Application(const std::string &app_name, glm::uvec2 window_size);
    ~Application();

    void run();

    static Application &get(void);

    SDL_Window *getWindow(void) const { return this->window; }


   private:
  };


  // We provide a nice ren::world() global function to access the world from anywhere in the code.
  // It may seem like a bit of an abstraction leakage, but I think it's important to let anything
  // access the low-level ECS world without having to go through an abstraction which would restrict
  // functionality.
  static inline flecs::world &world(void) { return ren::Application::get().world; }
  static inline float deltaTime(void) { return ren::world().delta_time(); }


}  // namespace ren