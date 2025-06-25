#pragma once

#include <ren/types.h>
#include <ren/core/UUID.h>
#include <ren/misc/json_serialize.h>

#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>
#include <ren/assets/Mesh.h>

namespace ren {

  namespace comp {

    // Entity components are simple PoD structs.


    struct ID {
      UUID uuid;

      NLOHMANN_DEFINE_TYPE_INTRUSIVE(ID, uuid);

      ID() = default;
      ID(UUID id)
          : uuid(id) {}
      ID(u64 id)
          : uuid(id) {}

      operator UUID() const { return uuid; }
      operator u64() const { return (u64)uuid; }
    };




    struct Name {
      std::string name;

      NLOHMANN_DEFINE_TYPE_INTRUSIVE(Name, name);

      Name() = default;
      Name(const std::string& name)
          : name(name) {}
      Name(std::string&& name)
          : name(std::move(name)) {}
    };

    struct Transform {
      glm::vec3 translation = {0.0f, 0.0f, 0.0f};
      glm::vec3 rotation = {0.0f, 0.0f, 0.0f};
      glm::vec3 scale = {1.0f, 1.0f, 1.0f};

      NLOHMANN_DEFINE_TYPE_INTRUSIVE(Transform, translation, rotation, scale);

      Transform() = default;
      Transform(const Transform&) = default;
      Transform(const glm::vec3& translation)
          : translation(translation) {}

      glm::mat4 getTransform() const {
        glm::mat4 rotation = glm::toMat4(glm::quat(rotation));

        return glm::translate(glm::mat4(1.0f), translation) * rotation *
               glm::scale(glm::mat4(1.0f), scale);
      }
    };


    class Mesh {
      ref<ren::Mesh> mesh;

     public:
      Mesh() = default;
      Mesh(ref<ren::Mesh> mesh)
          : mesh(mesh) {}
    };



  }  // namespace comp

}  // namespace ren