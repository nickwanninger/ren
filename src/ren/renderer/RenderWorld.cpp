#include "RenderWorld.h"



namespace ren {

  bool RenderWorld::isFrustumCulled(const AABB &modelSpaceAABB, const glm::mat4 &transform) const {
    // Transform all 8 corners of the AABB to world space
    glm::vec3 worldCorners[8] = {
        transform *
            glm::vec4(modelSpaceAABB.min.x, modelSpaceAABB.min.y, modelSpaceAABB.min.z, 1.0f),
        transform *
            glm::vec4(modelSpaceAABB.max.x, modelSpaceAABB.min.y, modelSpaceAABB.min.z, 1.0f),
        transform *
            glm::vec4(modelSpaceAABB.min.x, modelSpaceAABB.max.y, modelSpaceAABB.min.z, 1.0f),
        transform *
            glm::vec4(modelSpaceAABB.max.x, modelSpaceAABB.max.y, modelSpaceAABB.min.z, 1.0f),
        transform *
            glm::vec4(modelSpaceAABB.min.x, modelSpaceAABB.min.y, modelSpaceAABB.max.z, 1.0f),
        transform *
            glm::vec4(modelSpaceAABB.max.x, modelSpaceAABB.min.y, modelSpaceAABB.max.z, 1.0f),
        transform *
            glm::vec4(modelSpaceAABB.min.x, modelSpaceAABB.max.y, modelSpaceAABB.max.z, 1.0f),
        transform *
            glm::vec4(modelSpaceAABB.max.x, modelSpaceAABB.max.y, modelSpaceAABB.max.z, 1.0f),
    };

    // Get view-projection matrix
    glm::mat4 view = camera.view_matrix();
    glm::mat4 viewProj = Camera::projectionMatrix(320.0f, 240.0f) * view;

    // Check if any corner is inside the frustum
    for (int i = 0; i < 8; ++i) {
      glm::vec4 clipSpace = viewProj * glm::vec4(worldCorners[i], 1.0f);

      // Only consider points in front of the camera (w > 0)
      if (clipSpace.w > 0.0f) {
        clipSpace /= clipSpace.w;

        // Vulkan NDC: x, y in [-1, 1], z in [0, 1]
        if (clipSpace.x >= -1.0f && clipSpace.x <= 1.0f && clipSpace.y >= -1.0f &&
            clipSpace.y <= 1.0f && clipSpace.z >= 0.0f && clipSpace.z <= 1.0f) {
          // Found at least one corner inside frustum, object is visible
          return false;
        }
      }
    }

    // All corners are outside the frustum
    return true;
  }


  void RenderWorld::extractFromECS(flecs::world &world) {
    renderables.clear();
    pointLights.clear();

    // Query the ECS for entities with Mesh and Transform components
    world
        .query<comp::Mesh, comp::Transform, comp::Material>(
            "ren::core::RenderWorld::extractFromECS")
        .each([&](const comp::Mesh &mesh, const comp::Transform &transform,
                  const comp::Material &material) {
          Renderable r;
          r.mesh = mesh.mesh;
          r.transform = transform.transformMatrix;
          r.material = material.material;

          auto &aabb = mesh.mesh->getAABB();
          // if (isFrustumCulled(aabb, r.transform)) {
          //   return;  // Skip culled renderables
          // }

          renderables.push_back(std::move(r));
        });


    // Gather point lights from the ECS using the transform and PointLightComponent
    world
        .query<comp::Transform, ren::PointLightComponent>(
            "ren::core::RenderWorld::extractPointLights")
        .each([&](const comp::Transform &transform, const ren::PointLightComponent &plc) {
          PointLight pl;
          pl.position = glm::vec3(transform.transformMatrix[3]);
          pl.color = plc.color;
          pl.intensity = plc.intensity;
          pl.radius = plc.radius;
          pointLights.push_back(std::move(pl));
        });
    fmt::println("Extracted {} renderables and {} point lights from ECS", renderables.size(),
                 pointLights.size());

    // sort renderables by distance to camera
    glm::vec3 cameraPos = glm::inverse(camera.view_matrix())[3];
    std::sort(renderables.begin(), renderables.end(),
              [&cameraPos](const Renderable &a, const Renderable &b) {
                float distA = glm::length(glm::vec3(a.transform[3]) - cameraPos);
                float distB = glm::length(glm::vec3(b.transform[3]) - cameraPos);
                return distA < distB;
              });
  }

}  // namespace ren