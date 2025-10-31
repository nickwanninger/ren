#pragma once

#include <ren/renderer/Vulkan.h>
#include <ren/layers/LayerStack.h>
#include <SDL2/SDL.h>
#include <ren/renderer/RenderPass.h>
#include <ren/renderer/Renderer.h>
#include <ren/core/Scene.h>
#include <ren/core/Entity.h>
#include <ren/layers/SceneLayer.h>
#include <ren/assets/AssetManager.h>
#include <ren/core/FramerateCounter.h>
#include <flecs/flecs.h>

#include <sol/sol.hpp>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <luajit.h>
}

#ifndef REN_GIT_REVISION
  #define REN_GIT_REVISION "UNK"
#endif

#ifndef REN_BUILD_DATE
  #define REN_BUILD_DATE "UNK"
#endif

namespace ren {



  class Application {
    // The first important thing in an application is the SDL Window and the Vulkan instance.
    SDL_Window *window = nullptr;
    ref<Renderer> renderer;

    ren::LayerStack layerStack;

    bool running = true;
    Entity globalEventEntity;
    std::vector<std::function<void(void)>> exitCallbacks;

    sol::state lua;

    struct ImGuiState {
      // TODO: move this elsewhere!
      FramerateCounter framerateCounter;            // Framerate counter for ImGui
      VkDescriptorPool imguiPool = VK_NULL_HANDLE;  // Descriptor pool for ImGui
    };
    void initImGui();

   public:
    ref<SceneLayer> sceneLayer = nullptr;

    ren::AssetManager &getAssetManager() { return world.get_mut<ren::AssetManager>(); }

    double timeSeconds = 0.0f;
    // Arguably the most important part of the application is the ECS world.
    // We try to put everything in the world, so that we can use flecs systems to process basically
    // everything.
    flecs::world world;
    Application(const std::string &app_name, glm::uvec2 window_size);
    ~Application();

    void run();

    static Application &get(void);

    auto &getLuaState(void) { return this->lua; }

    SDL_Window *getWindow(void) const { return this->window; }


    void atExit(std::function<void(void)> func) { exitCallbacks.push_back(func); }


    template <typename T>
    void emitEvent(const T &event) {
      globalEventEntity.emit<T>(event);
    }

    template <typename T>
    void emitEvent() {
      globalEventEntity.emit<T>();
    }

    template <typename T, typename Fn>
    void onEvent(const Fn &callback) {
      globalEventEntity.observe<T>(callback);
    }
  };


  // We provide a nice ren::world() global function to access the world from anywhere in the code.
  // It may seem like a bit of an abstraction leakage, but I think it's important to let anything
  // access the low-level ECS world without having to go through an abstraction which would restrict
  // functionality.
  static inline flecs::world &world(void) { return ren::Application::get().world; }
  static inline float deltaTime(void) { return ren::world().delta_time(); }
  static inline float timeSeconds(void) { return ren::Application::get().timeSeconds; }
  static inline Entity entity(void) { return ren::world().entity(); }


  template <typename T>
  void emit(const T &event) {
    ren::Application::get().emitEvent<T>(event);
  }
  template <typename T>
  void emit() {
    ren::Application::get().emitEvent<T>();
  }


  template <typename T, typename Fn>
  static inline void onEvent(const Fn &callback) {
    ren::Application::get().onEvent<T>(callback);
  }


  template <typename T>
  static inline T &resource(void) {
    return ren::world().get_mut<T>();
  }

  template <typename T>
  static inline bool hasResource(void) {
    return ren::world().has<T>();
  }

  template <typename T>
  static inline T &ensureResource(void) {
    if (hasResource<T>()) return resource<T>();

    auto &world = ren::world();
    world.emplace<T>();
    return resource<T>();
  }

  // TODO: This stuff should really just be a resource...
  static inline auto *lua(void) { return ren::Application::get().getLuaState().lua_state(); }
  static inline auto &sol(void) { return ren::Application::get().getLuaState(); }
  // TODO: This should be elsewhere. Application.hpp is getting too big.
  static inline auto evalFennel(const std::string_view &code) {
    auto &lua = ren::sol();
    sol::function lua_require = lua["require"];
    sol::table fennel = lua_require("assets.fennel");
    sol::function fennel_eval = fennel["eval"];
    return fennel_eval(code);
  }

  static inline auto evalLua(const std::string_view &code) { return ren::sol().script(code); }
}  // namespace ren