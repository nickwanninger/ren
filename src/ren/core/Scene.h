#pragma once


#include <ren/core/UUID.h>
#include <ren/core/Option.h>
#include <ren/obj/Types.h>
#include <map>

namespace ren {



  // Nodes store components by their registered schema names. Each typed value
  // owns component data and shares its type descriptor. Component descriptors
  // define the JSON format; the scene only stores identity and hierarchy:
  // {
  //   "components": {
  //     "Name": { "name": "floor" },
  //     "Transform": {
  //       "translation": { "x": 0, "y": 0, "z": 0 },
  //       "rotation":    { "x": 0, "y": 0, "z": 0, "w": 1 },
  //       "scale":       { "x": 1, "y": 1, "z": 1 }
  //     },
  //     "Mesh": { "key": "<mesh-store-key>" },
  //     "Material": { "key": "<material-store-key>" }
  //   },
  //   "children": []
  // }


  // Keeps authored data and its initial JSON together. Components without a
  // schema retain only JSON. Generated components capture their initial values.
  class SceneComponent {
   public:
    // Holds editable data when the component schema is available.
    Option<obj::Value> value = None;
    // Captures loaded or generated data. Later edits do not change this snapshot.
    json originalJson;

    // Writes current typed data, or the source payload without an available schema.
    json serialize(void) const;
  };


  class Scene;

  // Owns authored components and ordered children. The parent is derived during
  // loading and does not own the node. Loaded nodes contain Name and Transform.
  class SceneNode {
   public:
    // Constructs an empty node that belongs to `s`.
    SceneNode(Scene &s)
        : scene(s) {}

    // Refers to the owning scene. The editor uses it later to construct children.
    Scene &scene;
    // Identifies this node within the scene file.
    UUID id;

    // Stores components by schema name, including those without available schemas.
    std::map<std::string, SceneComponent> components;

    // Refers to the parent, or is empty for a root.
    weak_ref<SceneNode> parent;
    // Preserves the order from the scene file.
    std::vector<ref<SceneNode>> children;

    // Returns a native snapshot. Throws if the component or its typed value is absent.
    // Change the snapshot and call set() to store a programmatic edit.
    template <typename T>
    T get() const {
      const auto &name = obj::getType<T>().name;
      const auto component = components.find(name);
      if (component == components.end()) {
        throw std::runtime_error("Missing component on node " + id.to_string() + ": " + name);
      }
      if (component->second.value.isNone()) {
        throw std::runtime_error("Component has no typed value on node " + id.to_string() + ": " + name);
      }
      return component->second.value.unwrap().template bake<T>();
    }


    // Adds or replaces authored data without editor history. Existing source JSON
    // stays intact; a new component captures its initial serialized values.
    template <typename T>
    void set(const T &native) {
      auto value = obj::readNative(native);
      const auto &name = value.type->name;
      const auto component = components.find(name);
      if (component != components.end()) {
        component->second.value = std::move(value);
        return;
      }

      auto originalJson = value.serialize();
      components.emplace(name, SceneComponent{.value = std::move(value), .originalJson = std::move(originalJson)});
    }
  };


  // Owns an authoring hierarchy and indexes nodes by file-local ID.
  // Loading and saving do not create ECS entities or resolve asset references.
  class Scene {
   public:
    // The version of the scene format.
    static constexpr u32 version = 1;
    // Constructs an empty scene with a new ID.
    Scene();

    // Replaces the scene after validating all nodes, components and relationships.
    // Invalid input throws and leaves the current scene intact.
    void load(const json &j);

    // Removes all nodes and roots while retaining the scene ID.
    void clear();
    // Writes components and ordered ID lists. Parent links are not serialized.
    json serialize(void) const;

   protected:
    UUID id;
    std::vector<ref<SceneNode>> roots;
    std::map<UUID, ref<SceneNode>> nodes;
  };
}  // namespace ren