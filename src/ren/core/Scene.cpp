#include <ren/core/Scene.h>
#include <ren/core/Entity.h>
#include <ren/core/Components.h>
#include <ren/core/Instrumentation.h>
#include <ren/core/AutoPlugin.h>
#include <ren/core/Application.h>

namespace ren {


  static void globalizeChildrenPlugin(ren::Application &app) {
    app.world.system<const comp::Transform *, comp::Transform>("ren::core::ApplyParentTransforms")
        .kind(flecs::PreStore)  // Run before the store phase
        .term_at(0)             // The first transform pointer is...
        .parent()               // ... from the parent
        .cascade()              // ... ordered top down
        .each([](const comp::Transform *parentTransform, comp::Transform &childTransform) {
          REN_PROFILE_SCOPE("Globalize Children Plugin");
          if (parentTransform) {
            childTransform.transformMatrix = parentTransform->transformMatrix * childTransform.getTransform();
          } else {
            childTransform.transformMatrix = childTransform.getTransform();
          }
        });
  }

  REN_PLUGIN("GlobalizeChildren", globalizeChildrenPlugin);


  Scene::Scene() {
    REN_DEPRECATION_WARNING();
    //
  }

}  // namespace ren