#include <ren/core/Scene.h>
#include <ren/core/Entity.h>
#include <ren/core/Components.h>
#include <ren/core/Instrumentation.h>
#include <ren/core/AutoPlugin.h>
#include <ren/core/Application.h>
#include <set>

namespace ren {


  //=========================
  // Transform plugin
  //=========================

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



  //=========================
  // Scene loading helpers
  //=========================

  static UUID readSceneID(const json &value, const std::string &path) {
    if (!value.is_string()) {
      throw std::runtime_error("Invalid ID at " + path + ": Expected 16 hex digits");
    }

    const auto &text = value.get_ref<const std::string &>();
    const auto parsed = UUID::parse(text);
    if (text.size() != 16 || !parsed || parsed.unwrap() == UUID(UUID::null)) {
      throw std::runtime_error("Invalid ID at " + path + ": " + text);
    }
    return parsed.unwrap();
  }


  // Returns the registration that has an authoring schema for `name`, or null.
  static const ComponentRegistrationInfo *findAuthoringRegistration(const std::string &name) {
    for (const auto &info : getRegisteredComponents()) {
      if (info.type != nullptr && info.makeValue != nullptr && info.type->name == name) {
        return &info;
      }
    }
    return nullptr;
  }


  static void loadSceneComponents(SceneNode &node, const json &components, const std::string &path) {
    if (!components.is_object()) {
      throw std::runtime_error("Expected component object at " + path);
    }

    // Every node has a Name and a Transform, even when the file omits them.
    const auto defaultName = obj::readNative(comp::Name("Entity"));
    const auto defaultTransform = obj::readNative(comp::Transform{});
    node.components.emplace("Name", SceneComponent{.value = defaultName, .originalJson = defaultName.serialize()});
    node.components.emplace("Transform", SceneComponent{.value = defaultTransform, .originalJson = defaultTransform.serialize()});

    for (const auto &[name, document] : components.items()) {
      auto &component = node.components[name];
      component.originalJson = document;

      // Components without a schema keep only their JSON.
      const auto *registration = findAuthoringRegistration(name);
      if (registration == nullptr) {
        continue;
      }

      // Name and Transform start from the defaults above; other components start from T{}.
      const auto initial = component.value ? component.value.unwrap() : registration->makeValue();
      component.value = initial.deserialize(document, path + "/" + name);
    }
  }


  static std::vector<UUID> readSceneNodeList(const json &list, const std::string &path, const std::map<UUID, ref<SceneNode>> &nodes,
                                             std::set<UUID> &placed) {
    if (!list.is_array()) {
      throw std::runtime_error("Expected node ID array at " + path);
    }

    std::vector<UUID> ids;
    ids.reserve(list.size());
    for (size_t i = 0; i < list.size(); ++i) {
      const auto address = path + "/" + std::to_string(i);
      const auto id = readSceneID(list[i], address);
      if (!nodes.contains(id)) {
        throw std::runtime_error("Missing node at " + address + ": " + id.to_string());
      }
      if (!placed.insert(id).second) {
        throw std::runtime_error("Node appears more than once at " + address + ": " + id.to_string());
      }
      ids.push_back(id);
    }
    return ids;
  }


  // Throws if a node is not reachable from a root. Each ID appears in at most
  // one list, so a root cannot be part of a cycle, and the nodes of a cycle stay
  // unreachable.
  static void verifyReachable(const std::vector<UUID> &rootIDs, const std::map<UUID, std::vector<UUID>> &childIDs,
                              const std::map<UUID, ref<SceneNode>> &nodes) {
    std::vector<UUID> pending = rootIDs;
    std::set<UUID> reachable;
    while (!pending.empty()) {
      const auto nodeID = pending.back();
      pending.pop_back();
      reachable.insert(nodeID);

      const auto &children = childIDs.at(nodeID);
      pending.insert(pending.end(), children.begin(), children.end());
    }

    for (const auto &[nodeID, node] : nodes) {
      if (!reachable.contains(nodeID)) {
        throw std::runtime_error("Unreachable node or cycle at nodes/" + nodeID.to_string());
      }
    }
  }



  //=========================
  // SceneComponent
  //=========================

  json SceneComponent::serialize(void) const {
    if (value) {
      return value.unwrap().serialize();
    }
    return originalJson;
  }



  //=========================
  // Scene
  //=========================

  Scene::Scene()
      : id(UUID::generate()) {}


  void Scene::clear() {
    roots.clear();
    nodes.clear();
  }


  void Scene::load(const json &j) {
    const bool hasVersion = j.is_object() && j.contains("version") && j["version"].is_number_integer();
    if (!hasVersion || j["version"] != Scene::version) {
      throw std::runtime_error("Scene version mismatch");
    }
    if (!j.contains("id")) {
      throw std::runtime_error("Scene missing id");
    }
    if (!j.contains("nodes") || !j["nodes"].is_object()) {
      throw std::runtime_error("Invalid nodes format");
    }
    if (!j.contains("roots")) {
      throw std::runtime_error("Invalid roots format");
    }

    // Keep the current scene intact until all input succeeds.
    Scene loaded;
    loaded.id = readSceneID(j["id"], "id");

    // Create every node before any children list is read, because a parent can
    // refer to a child that comes later in key order.
    for (const auto &[key, document] : j["nodes"].items()) {
      const auto path = "nodes/" + key;
      const auto nodeID = readSceneID(key, path);
      if (!document.is_object()) {
        throw std::runtime_error("Expected node object at " + path);
      }

      // Nodes refer to *this, not `loaded`, because `loaded` moves into *this
      // at the end and is then destroyed.
      auto node = ren::make<SceneNode>(*this);
      node->id = nodeID;
      loadSceneComponents(*node, document.value("components", json::object()), path + "/components");
      if (!loaded.nodes.emplace(nodeID, std::move(node)).second) {
        throw std::runtime_error("Duplicate node ID at " + path);
      }
    }

    // `placed` makes sure that each node appears in only one list.
    std::set<UUID> placed;
    const auto rootIDs = readSceneNodeList(j["roots"], "roots", loaded.nodes, placed);
    std::map<UUID, std::vector<UUID>> childIDs;
    for (const auto &[key, document] : j["nodes"].items()) {
      const auto path = "nodes/" + key;
      const auto nodeID = readSceneID(key, path);
      const auto children = document.value("children", json::array());
      childIDs.emplace(nodeID, readSceneNodeList(children, path + "/children", loaded.nodes, placed));
    }

    // Verify with IDs before linking, because a cycle of shared refs would leak.
    verifyReachable(rootIDs, childIDs, loaded.nodes);

    for (const auto nodeID : rootIDs) {
      loaded.roots.push_back(loaded.nodes.at(nodeID));
    }
    for (const auto &[nodeID, children] : childIDs) {
      const auto &node = loaded.nodes.at(nodeID);
      for (const auto childID : children) {
        const auto &child = loaded.nodes.at(childID);
        node->children.push_back(child);
        child->parent = node;
      }
    }

    *this = std::move(loaded);
  }


  json Scene::serialize(void) const {
    json j;
    j["version"] = Scene::version;
    j["id"] = id.to_string();
    j["nodes"] = json::object();
    for (const auto &[key, node] : nodes) {
      json components = json::object();
      for (const auto &[name, component] : node->components) {
        components[name] = component.serialize();
      }

      json children = json::array();
      for (const auto &child : node->children) {
        children.push_back(child->id.to_string());
      }
      j["nodes"][key.to_string()] = {{"children", std::move(children)}, {"components", std::move(components)}};
    }

    j["roots"] = json::array();
    for (const auto &root : roots) {
      j["roots"].push_back(root->id.to_string());
    }

    return j;
  }

}  // namespace ren
