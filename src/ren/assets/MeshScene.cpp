#include <ren/assets/MeshScene.hpp>

#include <imgui.h>
#include <math.h>

#include <ren/assets/materials/PBRMaterial.h>


#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>


namespace ren {


  glm::vec3 toEuler(const glm::quat &quaternion) {
    auto q = glm::normalize(quaternion);
    glm::vec3 res;

    double sinr_cosp = +2.0 * (q.w * q.x + q.y * q.z);
    double cosr_cosp = +1.0 - 2.0 * (q.x * q.x + q.y * q.y);
    res.x = atan2(sinr_cosp, cosr_cosp);

    double sinp = +2.0 * (q.w * q.y - q.z * q.x);
    if (abs(sinp) >= 1) {
      float sign = sinp < 0 ? -1.0f : 1.0f;
      res.y = M_PI / 2 * sign;
    } else {
      res.y = asin(sinp);
    }

    double siny_cosp = +2.0 * (q.w * q.z + q.x * q.y);
    double cosy_cosp = +1.0 - 2.0 * (q.y * q.y + q.z * q.z);
    res.z = atan2(siny_cosp, cosy_cosp);

    return res;
  }

  static void renderNodeTreeImGui(const MeshScene::Node &node) {
    ImGui::PushID(&node);

    if (ImGui::TreeNode(node.name.c_str())) {
      ImGui::Text("Mesh: %s", node.mesh ? node.mesh->getName().c_str() : "None");
      ImGui::Text("Transform: ");
      ImGui::Text("  Translation: (%f, %f, %f)", node.transform.translation.x,
                  node.transform.translation.y, node.transform.translation.z);
      ImGui::Text("  Rotation: (x:%f, y:%f, z:%f, w:%f)", node.transform.rotation.x,
                  node.transform.rotation.y, node.transform.rotation.z, node.transform.rotation.w);
      auto euler = glm::eulerAngles(node.transform.rotation);

      auto printAngles = [&](const char *order, glm::vec3 angles) {
        ImGui::Text("  Euler (%s): (x:%7.2f, y:%7.2f, z:%7.2f)", order, glm::degrees(angles.x),
                    glm::degrees(angles.y), glm::degrees(angles.z));

        // turn that angle back into a quaternion and print it
        glm::quat quat = glm::normalize(glm::quat(angles));
        ImGui::Text("               (x:%7.2f, y:%7.2f, z:%7.2f, w:%7.2f)", quat.x, quat.y, quat.z,
                    quat.w);
      };

      printAngles("RAW", euler);

      glm::mat4 rotMatrix = glm::mat4_cast(node.transform.rotation);

// #define TEST(order)                                                      \
//   {                                                                      \
//     glm::extractEulerAngle##order(rotMatrix, euler.x, euler.y, euler.z); \
//     printAngles(#order, euler);                                          \
//   }
//       TEST(XYZ)
//       TEST(YXZ)
//       TEST(XZX)
//       TEST(XYX)
//       TEST(YXY)
//       TEST(YZY)
//       TEST(ZYZ)
//       TEST(ZXZ)
//       TEST(XZY)
//       TEST(YZX)
//       TEST(ZYX)
//       TEST(ZXY)
// #undef TEST


      ImGui::Text("  Scale: (%f, %f, %f)", node.transform.scale.x, node.transform.scale.y,
                  node.transform.scale.z);

      if (not node.children.empty()) {
        ImGui::Separator();
        for (const auto &child : node.children) {
          renderNodeTreeImGui(*child);
        }
      }

      ImGui::TreePop();
    }

    ImGui::PopID();
  }

  void MeshScene::onImguiRender(void) {
    // Render the scene nodes.
    ImGui::PushID(this);


    if (ImGui::TreeNode("All Nodes")) {
      ImGui::PushID(&nodes);
      for (const auto &node : nodes) {
        if (ImGui::TreeNode(node->name.c_str())) {
          ImGui::Text("Mesh: %s", node->mesh ? node->mesh->getName().c_str() : "None");
          ImGui::Text("Transform: ");
          ImGui::Text("  Translation: (%f, %f, %f)", node->transform.translation.x,
                      node->transform.translation.y, node->transform.translation.z);
          // rotation quaternion
          ImGui::Text("  Rotation: (%f, %f, %f, %f)", node->transform.rotation.w,
                      node->transform.rotation.x, node->transform.rotation.y,
                      node->transform.rotation.z);
          ImGui::Text("  Scale: (%f, %f, %f)", node->transform.scale.x, node->transform.scale.y,
                      node->transform.scale.z);

          ImGui::TreePop();
        }
      }
      ImGui::TreePop();
      ImGui::PopID();
    }


    // Display all the meshes in the scene.
    if (ImGui::TreeNode("All Meshes")) {
      ImGui::PushID(&meshes);
      for (const auto &mesh : meshes) {
        if (ImGui::TreeNode(mesh->getName().c_str())) {
          ImGui::Text("Vertices: %u", mesh->getVertexCount());
          ImGui::Text("Indices: %u", mesh->getIndexCount());
          auto &aabb = mesh->getAABB();
          ImGui::Text("AABB: (%f, %f, %f) - (%f, %f, %f)", aabb.min.x, aabb.min.y, aabb.min.z,
                      aabb.max.x, aabb.max.y, aabb.max.z);
          ImGui::TreePop();
        }
      }
      ImGui::PopID();
      ImGui::TreePop();
    }

    // Display all the materials in the scene.
    if (ImGui::TreeNode("All Materials")) {
      ImGui::PushID(&materials);
      for (const auto &material : materials) {
        if (ImGui::TreeNode(material->getName().c_str())) {
          material->inspect();
          ImGui::TreePop();
        }
      }
      ImGui::PopID();
      ImGui::TreePop();
    }


    if (ImGui::TreeNode("Scene")) {
      for (auto &node : rootNodes) {
        renderNodeTreeImGui(*node);
      }
      ImGui::TreePop();
    }


    ImGui::PopID();
  }




  static void instantiateNode(ren::Scene &scene, const MeshScene::Node &node, Entity parentEntity) {
    // Create an entity for this node.
    Entity entity = scene.createEntity(node.name).child_of(parentEntity);
    entity.set<comp::Transform>(node.transform);  // Set the transform component.


    auto &mat = entity.set<comp::Material>({.material = node.material});


    // If the node has a mesh, add it to the entity.
    if (node.mesh) { entity.emplace<comp::Mesh>(node.mesh); }

    // Add the entity as a child of the parent entity.
    // if (parentEntity) parentEntity.addChild(entity);

    // Recursively instantiate child nodes.
    for (const auto &child : node.children) {
      instantiateNode(scene, *child, entity);
    }
  }

  Entity MeshScene::instantiate(ren::Scene &scene) {
    // Create an entity for each root node in the scene.
    Entity rootEntity = scene.createEntity("MeshSceneRoot");

    // Add the root nodes as children of the root entity.
    for (const auto &node : rootNodes) {
      instantiateNode(scene, *node, rootEntity);
    }

    return rootEntity;
  }


  ref<MeshScene> MeshScene::load(const std::filesystem::path &filename) {
    REN_PROFILE_SCOPE("Load Mesh Scene");

    // Load a mesh scene using assimp, not tinygltf.
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(
        filename.string(), aiProcess_Triangulate);
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
      fmt::print("Error loading mesh scene: {}\n", importer.GetErrorString());
      return nullptr;
    }


    auto meshScene = makeRef<MeshScene>();
    meshScene->meshes.reserve(scene->mNumMeshes);

    // Iterate over all meshes in the scene.
    for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
      REN_PROFILE_SCOPE("Read Assimp Mesh");
      const aiMesh *assimpMesh = scene->mMeshes[i];

      std::vector<Vertex> vertices;
      std::vector<u32> indices;

      // Read vertices
      vertices.reserve(assimpMesh->mNumVertices);
      for (unsigned int j = 0; j < assimpMesh->mNumVertices; ++j) {
        Vertex vertex;
        vertex.pos.x = assimpMesh->mVertices[j].x;
        vertex.pos.y = assimpMesh->mVertices[j].y;
        vertex.pos.z = assimpMesh->mVertices[j].z;

        if (assimpMesh->HasNormals()) {
          vertex.normal.x = assimpMesh->mNormals[j].x;
          vertex.normal.y = assimpMesh->mNormals[j].y;
          vertex.normal.z = assimpMesh->mNormals[j].z;
        } else {
          vertex.normal = glm::vec3(0.0f, 0.0f, 1.0f);  // Default normal if not present
        }
        if (assimpMesh->HasTextureCoords(0)) {
          vertex.texCoord.x = assimpMesh->mTextureCoords[0][j].x;
          vertex.texCoord.y = assimpMesh->mTextureCoords[0][j].y;
        } else {
          vertex.texCoord = glm::vec2(0.0f, 0.0f);  // Default texture coordinate if not present
        }
        vertices.push_back(vertex);
      }
      // Read indices
      indices.reserve(assimpMesh->mNumFaces * 3);  // Assuming triangles
      for (unsigned int j = 0; j < assimpMesh->mNumFaces; ++j) {
        const aiFace &face = assimpMesh->mFaces[j];
        if (face.mNumIndices == 3) {  // Only triangles
          indices.push_back(face.mIndices[0]);
          indices.push_back(face.mIndices[1]);
          indices.push_back(face.mIndices[2]);
        } else {
          fmt::print("Warning: Non-triangle face found in mesh '{}', skipping face.\n",
                     assimpMesh->mName.C_Str());
        }
      }
      // Create a mesh from the vertices and indices
      auto mesh = makeRef<Mesh>(assimpMesh->mName.C_Str(), vertices, indices);

      meshScene->meshes.push_back(mesh);
    }


    // create all the materials
    meshScene->materials.reserve(scene->mNumMaterials);
    for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
      REN_PROFILE_SCOPE("Read Assimp Material");
      const aiMaterial *assimpMaterial = scene->mMaterials[i];
      auto material = makeRef<ren::PBRMaterial>();

      // Set the material name
      aiString materialName;
      if (assimpMaterial->Get(AI_MATKEY_NAME, materialName) == aiReturn_SUCCESS) {
        material->setName(materialName.C_Str());
      } else {
        material->setName(fmt::format("Material_{}", i));
      }

      // Set the material properties
      aiColor3D color;
      if (assimpMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, color) == aiReturn_SUCCESS) {
        material->albedoColor = glm::vec3(color.r, color.g, color.b);
      }
      if (assimpMaterial->Get(AI_MATKEY_COLOR_SPECULAR, color) == aiReturn_SUCCESS) {
        material->specularColor = glm::vec3(color.r, color.g, color.b);
      }
      if (assimpMaterial->Get(AI_MATKEY_COLOR_EMISSIVE, color) == aiReturn_SUCCESS) {
        material->specularColor = glm::vec3(color.r, color.g, color.b);
      }

      meshScene->materials.push_back(material);
    }


    // Now we need to build the hierarchy of nodes.
    std::function<ref<MeshScene::Node>(const aiNode *assimpNode, MeshScene::Node &parentNode)>
        buildAssimpNodeHierarchy = [&](const aiNode *assimpNode, MeshScene::Node &parentNode) {
          REN_PROFILE_SCOPE("Build Assimp Node Hierarchy");
          auto node = makeRef<MeshScene::Node>();

          node->name = assimpNode->mName.C_Str();

          aiVector3t<float> scaling;
          aiQuaternion rotation;
          aiVector3t<float> translation;
          assimpNode->mTransformation.Decompose(scaling, rotation, translation);
          // Convert Assimp's aiVector3t and aiQuaternion to glm types
          node->transform.translation = glm::vec3(translation.x,translation.y, translation.z);
          node->transform.rotation = glm::quat(rotation.w, rotation.x, rotation.y, rotation.z);
          node->transform.scale = glm::vec3(scaling.x, scaling.y, scaling.z);

          node->mesh = nullptr;  // Default to no mesh


          // Add the child nodes
          for (unsigned int i = 0; i < assimpNode->mNumChildren; ++i) {
            REN_PROFILE_SCOPE("Add Child Node");
            auto child = buildAssimpNodeHierarchy(assimpNode->mChildren[i], *node);
            node->children.push_back(child);
          }

          if (assimpNode->mNumMeshes == 1) {
            // If this node has exactly one mesh, assign it.
            unsigned int meshIndex = assimpNode->mMeshes[0];
            if (meshIndex < meshScene->meshes.size()) {
              node->mesh = meshScene->meshes[meshIndex];
              node->material = meshScene->materials[scene->mMeshes[meshIndex]->mMaterialIndex];
            } else {
              fmt::print("Warning: Mesh index {} out of bounds for node '{}'\n", meshIndex,
                         node->name);
            }
          } else if (assimpNode->mNumMeshes > 0) {
            // Add a child for each mesh in the node.
            REN_PROFILE_SCOPE("Assign Meshes to Node");

            for (unsigned int j = 0; j < assimpNode->mNumMeshes; ++j) {
              // unsigned int meshIndex = assimpNode->mMeshes[j];
              // unsigned int materialIndex = scene->mMeshes[meshIndex]->mMaterialIndex;

              // if (meshIndex < meshScene->meshes.size()) {
              //   auto mesh = meshScene->meshes[meshIndex];
              //   auto material = meshScene->materials[materialIndex];
              //   node->mesh = mesh;
              //   node->material = material;
              // } else {
              //   fmt::print("Warning: Mesh index {} out of bounds for node '{}'\n", meshIndex,
              //              node->name);
              // }
            }
          }


          meshScene->nodes.push_back(node);
          fmt::println("{} has {} children and {} meshes", node->name, assimpNode->mNumChildren, assimpNode->mNumMeshes);
          // recurse
          for (unsigned int i = 0; i < assimpNode->mNumChildren; ++i) {
            REN_PROFILE_SCOPE("Recurse into Child Node");
            auto child = buildAssimpNodeHierarchy(assimpNode->mChildren[i], *node);
            node->children.push_back(child);
          }

          return node;
        };

    {
      REN_PROFILE_SCOPE("Build Assimp Node Hierarchy");
      meshScene->rootNodes.push_back(
          buildAssimpNodeHierarchy(scene->mRootNode, *makeRef<MeshScene::Node>()));
    }

    return meshScene;
  }
}  // namespace ren