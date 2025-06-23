#pragma once

#include <ren/types.h>
#include <ren/core/UUID.h>

#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>

namespace ren {
  // Entity components are simple PoD structs.


  namespace comp {

    struct ID {
      UUID uuid;

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

      Name() = default;
      Name(const std::string& name)
          : name(name) {}
      Name(std::string&& name)
          : name(std::move(name)) {}

      operator const std::string&() const { return name; }
      operator std::string&() { return name; }
    };


    struct Transform {
      glm::vec3 Translation = {0.0f, 0.0f, 0.0f};
      glm::vec3 Rotation = {0.0f, 0.0f, 0.0f};
      glm::vec3 Scale = {1.0f, 1.0f, 1.0f};

      Transform() = default;
      Transform(const Transform&) = default;
      Transform(const glm::vec3& translation)
          : Translation(translation) {}

      glm::mat4 getTransform() const {
        glm::mat4 rotation = glm::toMat4(glm::quat(Rotation));

        return glm::translate(glm::mat4(1.0f), Translation) * rotation *
               glm::scale(glm::mat4(1.0f), Scale);
      }
    };



  }  // namespace comp

}  // namespace ren