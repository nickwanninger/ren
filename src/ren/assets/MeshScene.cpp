#include <ren/assets/MeshScene.hpp>

#include <imgui/imgui.h>
#include <math.h>

#include <ren/assets/Material.h>
#include <ren/renderer/shader/ShaderCache.h>
#include <assimp/GltfMaterial.h>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <ren/core/ThreadPool.h>


using json = nlohmann::json;

std::string getPropertyTypeName(aiPropertyTypeInfo type) {
  switch (type) {
    case aiPTI_Float:
      return "float";
    case aiPTI_String:
      return "string";
    case aiPTI_Integer:
      return "integer";
    case aiPTI_Buffer:
      return "buffer";
    default:
      return "unknown";
  }
}


namespace ren {

  void MeshScene::onImguiRender(void) {}


  static Entity instantiateNode(const MeshScene::Node &node, Entity parentEntity) {
    // Create an entity for this node.
    Entity entity = ren::createEntity().set<comp::Name>(node.name);

    if (parentEntity) {
      entity.child_of(parentEntity);
    }
    entity.set<comp::Transform>(node.transform);  // Set the transform component.


    auto &mat = entity.set<comp::Material>({.material = node.material});


    // If the node has a mesh, add it to the entity.
    if (node.mesh) {
      entity.emplace<comp::Mesh>(node.mesh);
    }

    // Add the entity as a child of the parent entity.
    // if (parentEntity) parentEntity.addChild(entity);

    // Recursively instantiate child nodes.
    for (const auto &child : node.children) {
      instantiateNode(*child, entity);
    }
    return entity;
  }

  Entity MeshScene::instantiate(ren::Scene &scene) { return instantiateNode(*this->rootNode, scene.getRoot()); }

  Entity MeshScene::instantiate(ren::Entity parent) { return instantiateNode(*this->rootNode, parent); }



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
      if (matIdx < materials.size()) {
        node->material = materials[matIdx];
      }
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
        if (matIdx < materials.size()) {
          meshChild->material = materials[matIdx];
        }

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
    const aiScene *scene = REN_PROFILE_EXPR("ReadFile", importer.ReadFile(filename.string(), flags));
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
      fmt::print("Error loading mesh scene: {}\n", importer.GetErrorString());
      return nullptr;
    }


    auto meshScene = make<MeshScene>();
    meshScene->meshes.resize(scene->mNumMeshes);  // Preallocate space for meshes

    // Iterate over all meshes in the scene.
    // for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
    ren::parallel_for((int)scene->mNumMeshes, [&](int i) {
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
          fmt::print("Warning: Non-triangle face found in mesh '{}', skipping face.\n", assimpMesh->mName.C_Str());
        }
      }
      // Create a mesh from the vertices and indices
      auto mesh = make<Mesh>(assimpMesh->mName.C_Str(), vertices, indices);

      meshScene->meshes[i] = mesh;
    });


    // load all the textures
    // TODO(opt): load these on demand based on the needs of the meshes

    meshScene->textures.reserve(scene->mNumTextures);
    for (unsigned int i = 0; i < scene->mNumTextures; ++i) {
      meshScene->textures.push_back(nullptr);
    }

    // Textures that hold data instead of color must not be sRGB decoded.
    std::vector<bool> isDataTexture(scene->mNumTextures, false);
    for (unsigned int m = 0; m < scene->mNumMaterials; ++m) {
      for (aiTextureType type : {aiTextureType_NORMALS, aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_LIGHTMAP}) {
        aiString path;
        if (scene->mMaterials[m]->GetTexture(type, 0, &path) == aiReturn_SUCCESS && path.C_Str()[0] == '*') {
          unsigned int index = (unsigned int)atoi(&path.C_Str()[1]);
          if (index < isDataTexture.size()) {
            isDataTexture[index] = true;
          }
        }
      }
    }

    // Decode the embedded images on the thread pool. The decode is the slow
    // part, and it does no Vulkan work. The Texture constructor submits to
    // the queue, so this thread makes the textures after the decode.
    std::vector<ren::Texture::DecodedImage> decoded(scene->mNumTextures);
    {
      REN_PROFILE_SCOPE("Decode Assimp Textures");
      // An lvalue, so that parallel_for copies it into each chunk.
      auto decodeOne = [&](int i) {
        const aiTexture *assimpTexture = scene->mTextures[i];
        if (assimpTexture->mHeight == 0) {
          decoded[i] = ren::Texture::decode(assimpTexture->pcData, assimpTexture->mWidth);
        }
      };
      ren::parallel_for((int)scene->mNumTextures, decodeOne);
    }



    // Load the textures
    for (unsigned int i = 0; i < scene->mNumTextures; ++i) {
      REN_PROFILE_SCOPE("Load Assimp Texture");
      const aiTexture *assimpTexture = scene->mTextures[i];
      const VkFormat format = isDataTexture[i] ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_SRGB;
      ref<Texture> texture;
      if (assimpTexture->mHeight == 0) {
        std::string embeddedName = fmt::format("embedded_{}_{}", i, assimpTexture->mFilename.C_Str());
        auto &image = decoded[i];
        if (image) {
          texture = ren::Texture::create(embeddedName, image.width, image.height, image.pixels.get(), format);
        } else {
          ren::warnln("Failed to decode embedded texture '{}'", embeddedName);
        }
        // Free the pixels now. The upload copied them to a staging buffer.
        image = {};
      } else {
        texture = ren::Texture::load(assimpTexture->mFilename.C_Str(), format);
      }
      meshScene->textures[i] = texture;
    }


    // for (unsigned int i = 0; i < scene->mNumTextures; ++i) {
    //   const aiTexture *assimpTexture = scene->mTextures[i];
    //   ref<Texture> texture;
    //   if (assimpTexture->mHeight == 0) {
    //     std::string embeddedName =
    //         fmt::format("embedded_{}_{}", i, assimpTexture->mFilename.C_Str());
    //     texture = ren::Texture::load(embeddedName, assimpTexture->pcData, assimpTexture->mWidth);
    //   } else {
    //     texture = ren::Texture::load(assimpTexture->mFilename.C_Str());
    //   }
    //   meshScene->textures.push_back(texture);
    // }


    // create all the materials
    meshScene->materials.reserve(scene->mNumMaterials);
    for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
      REN_PROFILE_SCOPE("Read Assimp Material");
      const aiMaterial *assimpMaterial = scene->mMaterials[i];
      auto material = make<ren::Material>(ren::getShader("ren/core/pbr", "ren/pass/deferred"));

      // Set the material name
      aiString materialName;
      if (assimpMaterial->Get(AI_MATKEY_NAME, materialName) == aiReturn_SUCCESS) {
        material->setName(materialName.C_Str());
      } else {
        material->setName(fmt::format("{}_mat{}", filenameWithoutExtension, i));
      }

      // The material data starts as zeros. Set defaults, so that a key that
      // the file does not have does not give a black or mirror-like material.
      // The glTF importer always writes the factors, so these apply to other
      // formats. Metallic is 0 here, not the glTF default of 1, because such
      // files are mostly not metal.
      material->setColor("baseColor", glm::vec4(1.0f));
      material->setColor("emissiveFactor", glm::vec4(0.0f));
      material->setFloat("metallic", 0.0f);
      material->setFloat("roughness", 1.0f);
      material->setFloat("occlusionStrength", 1.0f);
      // material->getPSO().fillMode = ren::FillMode::Wireframe;


      float occlusionStrength = 1.0f;
      if (assimpMaterial->Get(AI_MATKEY_GLTF_TEXTURE_STRENGTH(aiTextureType_LIGHTMAP, 0), occlusionStrength) == aiReturn_SUCCESS) {
        occlusionStrength = glm::clamp(occlusionStrength, 0.0f, 1.0f);
        material->setFloat("occlusionStrength", occlusionStrength);
        printf("Material %s occlusion strength: %f\n", material->getName().c_str(), occlusionStrength);
      };
      // Set the material properties
      aiColor4D color;
      if (assimpMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, color) == aiReturn_SUCCESS) {
        material->setColor("baseColor", glm::vec4(color.r, color.g, color.b, color.a));
      }
      // if (assimpMaterial->Get(AI_MATKEY_COLOR_SPECULAR, color) == aiReturn_SUCCESS) {
      //   material->props.specularColor = glm::vec4(color.r, color.g, color.b, 1.0f);
      // }
      if (assimpMaterial->Get(AI_MATKEY_COLOR_EMISSIVE, color) == aiReturn_SUCCESS) {
        material->setColor("emissiveFactor", glm::vec4(color.r, color.g, color.b, color.a));
      }

      // metallic
      float metallicFactor;
      if (assimpMaterial->Get(AI_MATKEY_METALLIC_FACTOR, metallicFactor) == aiReturn_SUCCESS) {
        metallicFactor = glm::clamp(metallicFactor, 0.0f, 1.0f);
        material->setFloat("metallic", metallicFactor);
      }

      // roughness
      float roughnessFactor;
      if (assimpMaterial->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughnessFactor) == aiReturn_SUCCESS) {
        roughnessFactor = glm::clamp(roughnessFactor, 0.0f, 1.0f);
        material->setFloat("roughness", roughnessFactor);
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
          // ren::println("Texture: {}", path.C_Str());
        }
        return nullptr;
      };


      int twoSided;
      if (assimpMaterial->Get(AI_MATKEY_TWOSIDED, twoSided) == AI_SUCCESS) {
        if (twoSided) {
          material->getPSO().cullMode = ren::CullMode::None;
        }
      }


      if (auto diffuseTexture = grabTexture(aiTextureType_DIFFUSE)) {
        material->setTexture("baseColorTexture", diffuseTexture);
      }
      if (auto metallicRoughnessTexture = grabTexture(aiTextureType_DIFFUSE_ROUGHNESS)) {
        material->setTexture("metallicRoughnessTexture", metallicRoughnessTexture);
      }
      if (auto normalTexture = grabTexture(aiTextureType_NORMALS)) {
        material->setTexture("normalTexture", normalTexture);
      }
      if (auto occlusionTexture = grabTexture(aiTextureType_LIGHTMAP)) {
        material->setTexture("occlusionTexture", occlusionTexture);
      }

      if (auto emissiveTexture = grabTexture(aiTextureType_EMISSIVE)) {
        material->setTexture("emissiveTexture", emissiveTexture);
      }


      // auto j = dumpAiMaterial(assimpMaterial);
      // ren::println("{}", j.dump(2));

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
