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


  Scene::Scene() {}




  void Scene::clear() { nodes.clear(); }


  void Scene::load(json &j) {
    // verify that the version is correct
    if (!j.contains("version") || j["version"] != Scene::version) {
      throw std::runtime_error("Scene version mismatch");
    }


    // verify that the scene has an id
    if (!j.contains("id")) {
      throw std::runtime_error("Scene missing id");
    }

    this->id = j["id"];

    // now, the nodes, which better be an object where the keys are IDs, and the
    // values are nodes.
    if (j.contains("nodes") && j["nodes"].is_object()) {
      for (auto &[key, value] : j["nodes"].items()) {
        // verify that the key is a valid UUID
        if (key.size() != 16 || key.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
          throw std::runtime_error("Invalid node ID");
        }
        auto parsed = UUID::parse(key);
        if (!parsed) {
          throw std::runtime_error("Failed to parse node ID");
        }


        // verify that the value is an object
        if (!value.is_object()) {
          throw std::runtime_error("Invalid node format");
        }

        // create a new node and add it to the map
        auto node = std::make_shared<SceneNode>();
        node->id = parsed.unwrap();
        node->name = value.value("name", "");
        nodes[node->id] = node;
      }
    } else {
      throw std::runtime_error("Invalid nodes format");
    }


    // parse roots - they all must map to the nodes in the map
    // roots is an array of UUIDs.
    if (j.contains("roots") && j["roots"].is_array()) {
      for (auto &rootId : j["roots"]) {
        if (!rootId.is_string()) {
          throw std::runtime_error("Invalid root ID");
        }
        auto parsed = UUID::parse(rootId.get<std::string>());
        if (!parsed) {
          throw std::runtime_error("Failed to parse root ID");
        }
        auto it = nodes.find(parsed.unwrap());
        if (it == nodes.end()) {
          throw std::runtime_error("Invalid root node");
        }
        roots.push_back(it->second);
      }
    } else {
      throw std::runtime_error("Invalid roots format");
    }

    return;
  }


  json Scene::serialize(void) const {
    json j;
    j["version"] = Scene::version;
    j["id"] = id.to_string();
    j["nodes"] = json::object();
    for (const auto &[key, node] : nodes) {
      j["nodes"][key.to_string()] = {{"name", node->name}};
    }

    j["roots"] = json::array();
    for (const auto &root : roots) {
      j["roots"].push_back(root->id.to_string());
    }

    return j;
  }

}  // namespace ren
