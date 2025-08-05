#pragma once

#include <ren/assets/Mesh.h>
#include <ren/core/Components.h>
#include <ren/core/Scene.h>
#include <ren/core/Entity.h>
#include <ren/assets/Material.h>

namespace ren {


  class MeshScene : public ren::HasUUID {
   public:
    struct Node {
      // The name of this node from the file
      std::string name;
      // The mesh associated with this node.
      ref<Mesh> mesh;
      comp::Transform transform;

      // add a material
      ref<Material> material;

      // The child nodes of this node.
      std::vector<ref<Node>> children;  // The child nodes of this node.
    };

    std::vector<ref<Node>> nodes;   // The list of nodes in this scene.
    std::vector<ref<Mesh>> meshes;  // The meshes in this scene.
    std::vector<ref<Material>> materials;  // The list of materials in this scene.

    std::vector<ref<Node>> rootNodes;  // The list of root nodes in this scene.

    void onImguiRender(void);

    static ref<MeshScene> load(const std::filesystem::path &filename);


    // TOOD: temporary!
    Entity instantiate(ren::Scene &scene);
  };

}  // namespace ren
