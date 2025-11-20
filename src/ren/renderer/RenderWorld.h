#pragma once


#include <ren/core/Application.h>
#include <ren/Camera.h>
#include <ren/assets/Mesh.h>
#include <ren/assets/Material.h>


namespace ren {



  class RenderWorld {
   public:
    // Represents a single renderable object in the world
    struct Renderable {
      ref<Mesh> mesh;
      ref<Material> material;
      glm::mat4 transform;
    };

    struct PointLight {
      glm::vec3 position;  // World position
      glm::vec3 color;
      float intensity;
      float radius;
    };

    void extractFromECS(flecs::world &world);

    inline RenderWorld(ren::Camera &cam)
        : camera(cam) {};
    // no copy
    RenderWorld(const RenderWorld &) = delete;
    RenderWorld &operator=(const RenderWorld &) = delete;
    // no move
    RenderWorld(RenderWorld &&) = delete;
    RenderWorld &operator=(RenderWorld &&) = delete;

    ~RenderWorld() = default;

    std::vector<Renderable> renderables;
    std::vector<PointLight> pointLights;

   private:
    ren::Camera &camera;

    bool isFrustumCulled(const AABB &modelSpaceAABB, const glm::mat4 &transform) const;
  };
}  // namespace ren