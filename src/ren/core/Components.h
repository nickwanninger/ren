#pragma once

#include <ren/types.h>
#include <ren/core/UUID.h>
#include <entt/entt.hpp>
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


    // The Relationship component is used to define parent-child relationships
    // between entities.  It is used to create a transformation hierarchy, where
    // a parent entity's transformation affects its children.
    // This is meant to be used through the Entity abstraction, not directly.
    struct Relationship {
      // This is the parent entity of this entity.
      entt::entity parent = entt::null;
      size_t children = 0; // how many children this entity has.
      entt::entity firstChild = entt::null; // The first child of this entity.

      // We represent the siblings of an entity as a linked list.
      entt::entity prevSibling = entt::null; // The previous sibling of this entity.
      entt::entity nextSibling = entt::null; // The next sibling of this entity
    };


    struct Mesh {
      ref<ren::Mesh> mesh;


      Mesh() = default;
      Mesh(ref<ren::Mesh> mesh)
          : mesh(mesh) {}
    };



  }  // namespace comp

}  // namespace ren