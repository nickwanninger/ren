#pragma once


#include <ren/core/Application.h>
#include <ren/Camera.h>
#include <ren/assets/Mesh.h>
#include <ren/assets/Material.h>


namespace ren {


  using SceneProxyID = flecs::entity_t;


  struct ProxyCommands {
    ren::Arena arena;
  };

  struct RenderSceneProxy {
    flecs::world renderWorld;


    SceneProxyID createProxy() {
      renderWorld.async_stage();
      auto e = renderWorld.entity();
      return e.id();
    }


    template<typename T>
    void set(SceneProxyID proxy, T&& value) {
      auto compId = renderWorld.component<T>().id();
    }

  };

}  // namespace ren