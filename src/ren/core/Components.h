#pragma once

#include <ren/types.h>
#include <ren/core/UUID.h>
#include <ren/misc/json_serialize.h>
#include <ren/assets/Material.h>
#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>
#include <ren/assets/Mesh.h>

namespace ren {

  namespace comp {


    // These entities are those which are available to the editor and serialization.


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
      glm::quat rotation = {1.0f, 0.0f, 0.0f, 0.0f};
      glm::vec3 scale = {1.0f, 1.0f, 1.0f};


      // --- //

      // This value gets updated every frame to reflect the current *global*
      // transformations of the entity.  it is used for model->view
      // transformations, as well.
      glm::mat4 transformMatrix = glm::mat4(1.0f);

      NLOHMANN_DEFINE_TYPE_INTRUSIVE(Transform, translation, rotation, scale);

      Transform() = default;
      Transform(const Transform&) = default;
      Transform(const glm::vec3& translation)
          : translation(translation) {}

      glm::mat4 getTransform() const {
        return glm::translate(glm::mat4(1.0f), translation) *
               glm::mat4_cast(glm::normalize(rotation)) * glm::scale(glm::mat4(1.0f), scale);
      }
    };


    struct Mesh {
      ref<ren::Mesh> mesh;


      Mesh() = default;
      Mesh(ref<ren::Mesh> mesh)
          : mesh(mesh) {}
      friend void to_json(json& j, const Mesh& m) { j = m.mesh->getName(); }

      friend void from_json(const json& j, Mesh& uuid) { abort(); }
    };


    struct Material {
      ref<ren::Material> material;


      friend void to_json(json& j, const comp::Material& m) {
        j["name"] = m.material->getName();
        j["asset_id"] = m.material->getAssetID();
      }

      friend void from_json(const json& j, comp::Material& uuid) { abort(); }
    };



  }  // namespace comp

}  // namespace ren