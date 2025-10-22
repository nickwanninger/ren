#include <ren/assets/MeshScene.hpp>

#include <imgui/imgui.h>
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

    if (ImGui::TreeNode("All Textures")) {
      ImGui::PushID(&textures);
      for (const auto &texture : textures) {
        ImGui::PushID(texture.get());
        if (ImGui::TreeNode("texture")) {
          ImGui::Image(texture->getImGui(), ImVec2(512, 512));
          ImGui::TreePop();
        }
        ImGui::PopID();
      }
      ImGui::PopID();
      ImGui::TreePop();
    }


    if (ImGui::TreeNode("Scene")) {
      renderNodeTreeImGui(*rootNode);
      ImGui::TreePop();
    }


    ImGui::PopID();
  }




  static Entity instantiateNode(const MeshScene::Node &node, Entity parentEntity) {
    // Create an entity for this node.
    Entity entity = ren::createEntity().set<comp::Name>(node.name);

    if (parentEntity) entity.child_of(parentEntity);
    entity.set<comp::Transform>(node.transform);  // Set the transform component.


    auto &mat = entity.set<comp::Material>({.material = node.material});


    // If the node has a mesh, add it to the entity.
    if (node.mesh) { entity.emplace<comp::Mesh>(node.mesh); }

    // Add the entity as a child of the parent entity.
    // if (parentEntity) parentEntity.addChild(entity);

    // Recursively instantiate child nodes.
    for (const auto &child : node.children) {
      instantiateNode(*child, entity);
    }
    return entity;
  }

  Entity MeshScene::instantiate(ren::Scene &scene) {
    return instantiateNode(*this->rootNode, scene.getRoot());
  }

  Entity MeshScene::instantiate(ren::Entity parent) {
    return instantiateNode(*this->rootNode, parent);
  }



  ref<MeshScene::Node> MeshScene::convertAssimpNode(const aiNode *ainode, const aiScene *scene) {
    auto node = makeRef<MeshScene::Node>();
    node->name = ainode->mName.C_Str();
    this->nodes.push_back(node);  // Add to the scene's node list

    // Convert transform
    aiMatrix4x4 m = ainode->mTransformation;
    glm::mat4 transform = glm::transpose(glm::make_mat4(&m.a1));
    // Decompose transform
    glm::vec3 translation, scale, skew;
    glm::vec4 perspective;
    glm::quat rotation;
    glm::decompose(transform, scale, rotation, translation, skew, perspective);
    node->transform.translation = translation;
    node->transform.rotation = rotation;
    node->transform.scale = scale;

    // If this node has one mesh, set it directly
    if (ainode->mNumMeshes == 1) {
      unsigned meshIdx = ainode->mMeshes[0];
      node->mesh = this->meshes[meshIdx];

      // Assign material if available
      unsigned matIdx = scene->mMeshes[meshIdx]->mMaterialIndex;
      if (matIdx < materials.size()) { node->material = materials[matIdx]; }
    } else if (ainode->mNumMeshes > 1) {
      // If this node has more than one mesh, create a child for each mesh
      for (unsigned i = 0; i < ainode->mNumMeshes; ++i) {
        unsigned meshIdx = ainode->mMeshes[i];
        auto meshChild = makeRef<MeshScene::Node>();
        meshChild->name = node->name + fmt::format("_Mesh{}", i);
        meshChild->mesh = meshes[meshIdx];
        meshChild->transform = node->transform;

        // Assign material if available
        unsigned matIdx = scene->mMeshes[meshIdx]->mMaterialIndex;
        if (matIdx < materials.size()) { meshChild->material = materials[matIdx]; }

        this->nodes.push_back(meshChild);  // Add to the scene's node list
        node->children.push_back(meshChild);
      }

      // Since the node has many submeshes, we reset the transform to identity, and have the submeshes have the transform instea
      node->transform.translation = glm::vec3(0.0f);
      node->transform.rotation = glm::quat();
      node->transform.scale = glm::vec3(1.0f);
    }

    // Recursively convert children
    for (unsigned i = 0; i < ainode->mNumChildren; ++i) {
      auto child = convertAssimpNode(ainode->mChildren[i], scene);
      node->children.push_back(child);
    }

    return node;
  }

  ref<MeshScene> MeshScene::load(const std::filesystem::path &filename) {
    REN_PROFILE_SCOPE("Load Mesh Scene");

    std::string filenameWithoutExtension = filename.stem().string();

    // Load a mesh scene using assimp, not tinygltf.
    Assimp::Importer importer;
    unsigned int flags = 0;
    flags |= aiProcess_Triangulate;               // Ensure all meshes are triangulated
    flags |= aiProcess_FlipUVs;                   // Flip UVs to match Vulkan's
    flags |= aiProcess_EmbedTextures;             // Embed textures in the scene
    flags |= aiProcess_JoinIdenticalVertices;     // Join identical vertices
    flags |= aiProcess_CalcTangentSpace;          // Calculate tangent space
    flags |= aiProcess_OptimizeMeshes;            // Optimize meshes
    flags |= aiProcess_RemoveRedundantMaterials;  // Remove redundant materials
    // flags |= aiProcess_Debone; // DEBONE (temp)
    flags |= aiProcess_FindInstances;
    const aiScene *scene = importer.ReadFile(filename.string(), flags);
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
          vertex.normal = glm::normalize(vertex.normal);
        } else {
          vertex.normal = glm::vec3(0.0f, 0.0f, 1.0f);  // Default normal if not present
        }
        if (assimpMesh->HasTangentsAndBitangents()) {
          vertex.tangent.x = assimpMesh->mTangents[j].x;
          vertex.tangent.y = assimpMesh->mTangents[j].y;
          vertex.tangent.z = assimpMesh->mTangents[j].z;

          vertex.bitangent.x = assimpMesh->mBitangents[j].x;
          vertex.bitangent.y = assimpMesh->mBitangents[j].y;
          vertex.bitangent.z = assimpMesh->mBitangents[j].z;
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


    // load all the textures
    // TODO(opt): load these on demand based on the needs of the meshes
    meshScene->textures.reserve(scene->mNumTextures);
    for (unsigned int i = 0; i < scene->mNumTextures; ++i) {
      const aiTexture *assimpTexture = scene->mTextures[i];
      ref<Texture> texture;
      if (assimpTexture->mHeight == 0) {
        std::string embeddedName = fmt::format("embedded_{}_{}", i, assimpTexture->mFilename.C_Str());
        texture = ren::Texture::load(embeddedName, assimpTexture->pcData, assimpTexture->mWidth);
      } else {
        texture = ren::Texture::load(assimpTexture->mFilename.C_Str());
      }
      meshScene->textures.push_back(texture);
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
        material->setName(fmt::format("{}_mat{}", filenameWithoutExtension, i));
      }

      // Set the material properties
      aiColor4D color;
      if (assimpMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, color) == aiReturn_SUCCESS) {
        material->props.baseColorFactor = glm::vec4(color.r, color.g, color.b, color.a);
      }
      // if (assimpMaterial->Get(AI_MATKEY_COLOR_SPECULAR, color) == aiReturn_SUCCESS) {
      //   material->props.specularColor = glm::vec4(color.r, color.g, color.b, 1.0f);
      // }
      if (assimpMaterial->Get(AI_MATKEY_COLOR_EMISSIVE, color) == aiReturn_SUCCESS) {
        material->props.emissive = glm::vec4(color.r, color.g, color.b, color.a);
      }

      // metallic
      if (assimpMaterial->Get(AI_MATKEY_METALLIC_FACTOR, material->props.metallicFactor) ==
          aiReturn_SUCCESS) {
        material->props.metallicFactor = glm::clamp(material->props.metallicFactor, 0.0f, 1.0f);
      }

      // roughness
      if (assimpMaterial->Get(AI_MATKEY_ROUGHNESS_FACTOR, material->props.roughnessFactor) ==
          aiReturn_SUCCESS) {
        material->props.roughnessFactor = glm::clamp(material->props.roughnessFactor, 0.0f, 1.0f);
      }

      // textures
      auto grabTexture = [&](aiTextureType type) -> ref<Texture> {
        aiString path;
        if (assimpMaterial->GetTexture(type, 0, &path) == aiReturn_SUCCESS) {
          if (path.C_Str()[0] == '*') {
            // Embedded texture - path is "*0", "*1", etc.
            int embeddedIndex = atoi(&path.C_Str()[1]);
            return meshScene->textures[embeddedIndex];
          }
          fmt::println("Texture: {}", path.C_Str());
        }
        return nullptr;
      };


      if (auto diffuseTexture = grabTexture(aiTextureType_DIFFUSE)) {
        material->baseColorTexture = diffuseTexture;
      }
      if (auto metallicRoughnessTexture = grabTexture(aiTextureType_DIFFUSE_ROUGHNESS)) {
        material->metallicRoughnessTexture = metallicRoughnessTexture;
      }
      if (auto normalTexture = grabTexture(aiTextureType_NORMALS)) {
        material->normalTexture = normalTexture;
      }

      meshScene->materials.push_back(material);
    }


    // Build the node hierarchy starting from the root in the aiScene
    auto root = meshScene->convertAssimpNode(scene->mRootNode, scene);
    // set the name to the filename without extension
    root->name = fmt::format("{}", filenameWithoutExtension);
    meshScene->rootNode = root;

    return meshScene;
  }
}  // namespace ren