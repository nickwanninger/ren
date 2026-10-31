#pragma once


#include <ren/core/UUID.h>
#include <ren/core/ComponentRegistration.h>

namespace ren {



  // A component is pretty much always required to be small, trivial, and
  // copyable to the best of our ability. Also, in the C++ side of the engine,
  // components must be registered with the engine so they can be used in the
  // ECS, and they all feature a describe() method which helps the engine
  // understand how the componoent should be serialized and deserialized. As
  // part of this, the component registers its fields and their types, so the
  // engine can extract them and manipulate them. This is a bit of a pain right
  // now but when we integrate a C# game scripting layer, it gets much easier.
  //
  // So the fields are more than just a way to serialize/deserialize, they are
  // also a way to express how to author the component. For example, a Transform
  // component has a position, rotation, and scale. When authoring a Scene, the
  // engine can use this field information to allocate SceneComponents on a
  // node, and then the engine can display the fields for that SceneComponent in
  // the editor. The component can then be baked out into a runtime component
  // which flecs understands using the deserialization.  Therefore, we need to
  // come up with a way to express these fields in a way that can be efficiently
  // authored, serialized, and deserialized.
  //
  // I woud like for a scene node gets serialized as the following json structure:
  // {
  //   "components": {
  //     "Name": { "name": "floor" },
  //     "Transform": {
  //       "translation": ["vec3", [0, 0, 0]],
  //       "rotation":    ["quat", [0, 0, 0, 1]],
  //       "scale":       ["vec3", [1, 1, 1]],
  //     },
  //     "Mesh": { "key": "<mesh-store-key>" },
  //     "Material": { "key": "<material-store-key>" }
  //   },
  //   "children": []
  // }
  //
  // I think, a SceneComponent (authoring component) should basically just have
  // an array of abstract Field types, which come from template specialization,
  // in the ComponentRegistration setup. Each type (glm::vec3, etc) has a
  // corresponding subclassed SceneComponentField type generated that points to
  // the type that can serialize/deserialize, but the SceneComponentField can
  // contain a value of the type. Thus, it can be edited in a UI, and then
  // serialized/deserialized.


  struct Transform {
    glm::vec3 translation;
    glm::quat rotation;
    glm::vec3 scale;

  };

  // Represents a type of a field in a component.
  class EntityComponentFieldType {

  };

  // A field is a value of a field of a component. It has a type,
  // and can critically be serialized, deserialized, and baked into
  // a runtime component.
  class EntityComponentFieldBase {

    // Serialize the field into a json object.
    virtual void serialize(json &j) = 0;
    // Deserialize the field from a json object.
    virtual void deserialize(const json &j) = 0;

    // Bake the field into a runtime component. This is called when
    // the scene is writing out to the runtime representation. `dst`
    // is a pointer to the field in the Component on an Entity.
    virtual void bake(void *dst) = 0;
    // Inspect the field in a UI.
    virtual bool inspect(void) = 0;
  };


  template <typename T>
  class EntityComponentField : public EntityComponentFieldBase {
   public:
    T value;
  };




  // The document representation of a Component, which in the runtime is a flecs
  // component, but here is stored as a representation for authoring,
  // serialization, and deserialization.
  class SceneComponent {};

  // A scene entity is a node in the scene graph. It is the authoring/serialized
  // representation of a node in the scene. The Ren RUNTIME representation of a
  // node is a flecs entity with all the same components as the scene node.
  class SceneEntity {
   public:
    std::string name;

    // The components of this node.
  };

  // A scene is a collection of entities with components that can be rendered.
  class Scene {
   public:
    // Construct the scene using some entity as a root
    Scene();

   protected:
  };
}  // namespace ren
