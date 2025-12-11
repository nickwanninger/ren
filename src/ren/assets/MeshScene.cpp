#include <ren/assets/MeshScene.hpp>

#include <imgui/imgui.h>
#include <math.h>

#include <ren/assets/materials/PBRMaterial.h>
#include <assimp/GltfMaterial.h>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>




using json = nlohmann::json;

std::string getPropertyTypeName(aiPropertyTypeInfo type) {
  switch (type) {
    case aiPTI_Float: return "float";
    case aiPTI_String: return "string";
    case aiPTI_Integer: return "integer";
    case aiPTI_Buffer: return "buffer";
    default: return "unknown";
  }
}

json dumpAiMaterial(const aiMaterial *mat) {
  json result = json::object();

  result["numProperties"] = mat->mNumProperties;
  json properties = json::object();

  for (unsigned int i = 0; i < mat->mNumProperties; ++i) {
    const aiMaterialProperty *prop = mat->mProperties[i];
    std::string key = prop->mKey.C_Str();

    json propData = json::object();
    propData["type"] = getPropertyTypeName(prop->mType);
    propData["index"] = prop->mIndex;
    propData["dataLength"] = prop->mDataLength;

    // Try to extract value based on type
    json value;

    if (prop->mType == aiPTI_Float) {
      if (prop->mDataLength / sizeof(float) == 1) {
        float f;
        mat->Get(key.c_str(), aiTextureType_UNKNOWN, prop->mIndex, f);
        value = f;
      } else {
        json floatArray = json::array();
        float *data = reinterpret_cast<float *>(prop->mData);
        for (unsigned int j = 0; j < prop->mDataLength / sizeof(float); ++j) {
          floatArray.push_back(data[j]);
        }
        value = floatArray;
      }
    } else if (prop->mType == aiPTI_Integer) {
      if (prop->mDataLength / sizeof(int) == 1) {
        int i_val;
        mat->Get(key.c_str(), aiTextureType_UNKNOWN, prop->mIndex, i_val);
        value = i_val;
      } else {
        json intArray = json::array();
        int *data = reinterpret_cast<int *>(prop->mData);
        for (unsigned int j = 0; j < prop->mDataLength / sizeof(int); ++j) {
          intArray.push_back(data[j]);
        }
        value = intArray;
      }
    } else if (prop->mType == aiPTI_String) {
      aiString str;
      mat->Get(key.c_str(), aiTextureType_UNKNOWN, prop->mIndex, str);
      value = str.C_Str();
    } else if (prop->mType == aiPTI_Buffer) {
      // For binary data, just note it exists
      value = "(binary data, " + std::to_string(prop->mDataLength) + " bytes)";
    }

    propData["value"] = value;
    properties[key] = propData;
  }

  result["properties"] = properties;

  // Also dump texture information by type
  json textures = json::object();
  const aiTextureType textureTypes[] = {aiTextureType_DIFFUSE,
                                        aiTextureType_SPECULAR,
                                        aiTextureType_AMBIENT,
                                        aiTextureType_EMISSIVE,
                                        aiTextureType_HEIGHT,
                                        aiTextureType_NORMALS,
                                        aiTextureType_SHININESS,
                                        aiTextureType_OPACITY,
                                        aiTextureType_DISPLACEMENT,
                                        aiTextureType_LIGHTMAP,
                                        aiTextureType_REFLECTION,
                                        aiTextureType_BASE_COLOR,
                                        aiTextureType_NORMAL_CAMERA,
                                        aiTextureType_EMISSION_COLOR,
                                        aiTextureType_METALNESS,
                                        aiTextureType_DIFFUSE_ROUGHNESS,
                                        aiTextureType_AMBIENT_OCCLUSION,
                                        aiTextureType_UNKNOWN};

  const std::string textureTypeNames[] = {
      "DIFFUSE",           "SPECULAR",          "AMBIENT",       "EMISSIVE",       "HEIGHT",
      "NORMALS",           "SHININESS",         "OPACITY",       "DISPLACEMENT",   "LIGHTMAP",
      "REFLECTION",        "BASE_COLOR",        "NORMAL_CAMERA", "EMISSION_COLOR", "METALNESS",
      "DIFFUSE_ROUGHNESS", "AMBIENT_OCCLUSION", "UNKNOWN"};

  for (size_t t = 0; t < 18; ++t) {
    unsigned int count = mat->GetTextureCount(textureTypes[t]);
    if (count > 0) {
      json typeTextures = json::array();
      for (unsigned int i = 0; i < count; ++i) {
        aiString path;
        aiTextureMapping mapping;
        unsigned int uvIndex;
        float blend;
        aiTextureOp op;

        if (mat->GetTexture(textureTypes[t], i, &path, &mapping, &uvIndex, &blend, &op) ==
            AI_SUCCESS) {
          json texInfo = json::object();
          texInfo["path"] = path.C_Str();
          texInfo["uvIndex"] = uvIndex;
          texInfo["blend"] = blend;

          // Map enum values to strings
          std::string mappingStr = "UNKNOWN";
          if (mapping == aiTextureMapping_UV)
            mappingStr = "UV";
          else if (mapping == aiTextureMapping_SPHERE)
            mappingStr = "SPHERE";
          else if (mapping == aiTextureMapping_CYLINDER)
            mappingStr = "CYLINDER";
          else if (mapping == aiTextureMapping_BOX)
            mappingStr = "BOX";
          else if (mapping == aiTextureMapping_PLANE)
            mappingStr = "PLANE";
          texInfo["mapping"] = mappingStr;

          std::string opStr = "ADD";
          if (op == aiTextureOp_Multiply)
            opStr = "MULTIPLY";
          else if (op == aiTextureOp_Add)
            opStr = "ADD";
          else if (op == aiTextureOp_Subtract)
            opStr = "SUBTRACT";
          else if (op == aiTextureOp_Divide)
            opStr = "DIVIDE";
          else if (op == aiTextureOp_SmoothAdd)
            opStr = "SMOOTH_ADD";
          else if (op == aiTextureOp_SignedAdd)
            opStr = "SIGNED_ADD";
          texInfo["operation"] = opStr;

          typeTextures.push_back(texInfo);
        }
      }
      textures[textureTypeNames[t]] = typeTextures;
    }
  }

  result["textures"] = textures;

  // Dump common material colors
  json colors = json::object();

  aiColor3D color;
  if (mat->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS) {
    colors["diffuse"] = json::array({color.r, color.g, color.b});
  }
  if (mat->Get(AI_MATKEY_COLOR_SPECULAR, color) == AI_SUCCESS) {
    colors["specular"] = json::array({color.r, color.g, color.b});
  }
  if (mat->Get(AI_MATKEY_COLOR_AMBIENT, color) == AI_SUCCESS) {
    colors["ambient"] = json::array({color.r, color.g, color.b});
  }
  if (mat->Get(AI_MATKEY_COLOR_EMISSIVE, color) == AI_SUCCESS) {
    colors["emissive"] = json::array({color.r, color.g, color.b});
  }
  if (mat->Get(AI_MATKEY_COLOR_TRANSPARENT, color) == AI_SUCCESS) {
    colors["transparent"] = json::array({color.r, color.g, color.b});
  }

  result["colors"] = colors;

  // Dump common scalar properties
  json scalars = json::object();

  float shininess;
  if (mat->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS) { scalars["shininess"] = shininess; }

  float opacity;
  if (mat->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS) { scalars["opacity"] = opacity; }

  int twoSided;
  if (mat->Get(AI_MATKEY_TWOSIDED, twoSided) == AI_SUCCESS) {
    scalars["twoSided"] = (bool)twoSided;
  }

  result["scalars"] = scalars;

  return result;
}




namespace ren {

  void MeshScene::onImguiRender(void) {}


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
    auto node = make<MeshScene::Node>();
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
        auto meshChild = make<MeshScene::Node>();
        meshChild->name = node->name + fmt::format("_Mesh{}", i);
        meshChild->mesh = meshes[meshIdx];
        meshChild->transform = node->transform;

        // Assign material if available
        unsigned matIdx = scene->mMeshes[meshIdx]->mMaterialIndex;
        if (matIdx < materials.size()) { meshChild->material = materials[matIdx]; }

        this->nodes.push_back(meshChild);  // Add to the scene's node list
        node->children.push_back(meshChild);
      }

      // Since the node has many submeshes, we reset the transform to identity, and have the
      // submeshes have the transform instea
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
    flags |= aiProcess_Triangulate;            // Ensure all meshes are triangulated
    flags |= aiProcess_FlipUVs;                // Flip UVs to match Vulkan's
    flags |= aiProcess_EmbedTextures;          // Embed textures in the scene
    flags |= aiProcess_JoinIdenticalVertices;  // Join identical vertices
    flags |= aiProcess_CalcTangentSpace;       // Calculate tangent space
    // flags |= aiProcess_OptimizeMeshes;            // Optimize meshes
    flags |= aiProcess_RemoveRedundantMaterials;  // Remove redundant materials
    // flags |= aiProcess_Debone; // DEBONE (temp)
    flags |= aiProcess_FindInstances;
    const aiScene *scene = importer.ReadFile(filename.string(), flags);
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
      fmt::print("Error loading mesh scene: {}\n", importer.GetErrorString());
      return nullptr;
    }


    auto meshScene = make<MeshScene>();
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
        } else {
          vertex.tangent = glm::vec3(1.0f, 0.0f, 0.0f);    // Default tangent
          vertex.bitangent = glm::vec3(0.0f, 1.0f, 0.0f);  // Default bitangent
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
      auto mesh = make<Mesh>(assimpMesh->mName.C_Str(), vertices, indices);

      meshScene->meshes.push_back(mesh);
    }


    // load all the textures
    // TODO(opt): load these on demand based on the needs of the meshes
    meshScene->textures.reserve(scene->mNumTextures);
    for (unsigned int i = 0; i < scene->mNumTextures; ++i) {
      const aiTexture *assimpTexture = scene->mTextures[i];
      ref<Texture> texture;
      if (assimpTexture->mHeight == 0) {
        std::string embeddedName =
            fmt::format("embedded_{}_{}", i, assimpTexture->mFilename.C_Str());
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
      auto material = make<ren::PBRMaterial>();

      // Set the material name
      aiString materialName;
      if (assimpMaterial->Get(AI_MATKEY_NAME, materialName) == aiReturn_SUCCESS) {
        material->setName(materialName.C_Str());
      } else {
        material->setName(fmt::format("{}_mat{}", filenameWithoutExtension, i));
      }


      float occlusionStrength = 1.0f;
      if (assimpMaterial->Get(AI_MATKEY_GLTF_TEXTURE_STRENGTH(aiTextureType_LIGHTMAP, 0),
                              occlusionStrength) == aiReturn_SUCCESS) {
        // Got occlusion strength
        material->props.occlusionStrength = glm::clamp(occlusionStrength, 0.0f, 1.0f);
        printf("Material %s occlusion strength: %f\n", material->getName().c_str(),
               material->props.occlusionStrength);
      };
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
          // fmt::println("Texture: {}", path.C_Str());
        }
        return nullptr;
      };


      int twoSided;
      if (assimpMaterial->Get(AI_MATKEY_TWOSIDED, twoSided) == AI_SUCCESS) {
        if (twoSided) { material->getPSO().cullMode = ren::CullMode::None; }
      }


      if (auto diffuseTexture = grabTexture(aiTextureType_DIFFUSE)) {
        material->baseColorTexture = diffuseTexture;
      }
      if (auto metallicRoughnessTexture = grabTexture(aiTextureType_DIFFUSE_ROUGHNESS)) {
        material->metallicRoughnessTexture = metallicRoughnessTexture;
      }
      if (auto normalTexture = grabTexture(aiTextureType_NORMALS)) {
        material->normalTexture = normalTexture;
      }

      if (auto emissiveTexture = grabTexture(aiTextureType_EMISSIVE)) {
        material->emissiveTexture = emissiveTexture;
      }


      // auto j = dumpAiMaterial(assimpMaterial);
      // fmt::println("{}", j.dump(2));

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
