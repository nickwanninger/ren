#include <ren/types.h>
#include <ren/assets/AssetManager.h>
#include <ren/assets/Vertex.h>
#include <ren/assets/Mesh.h>

#include <assimp/GltfMaterial.h>
#include <assimp/Importer.hpp>
#include "ren/core/ThreadPool.h"
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <ren/core/Bundle.h>
#include <meshoptimizer/meshoptimizer.h>

namespace ren {


  struct MeshImport {
    std::string name;
    AABB boundingBox;
    std::vector<u32> indices;
    std::vector<Vertex> vertices;

    void process(void) {
      REN_PROFILE_SCOPE("MeshImport::process");

      {
        REN_PROFILE_SCOPE("Compute Bounding Box");
        for (const auto &vertex : vertices) {
          boundingBox.update(vertex.pos);
        }
      }


      {
        REN_PROFILE_SCOPE("BVH");
      }

      REN_PROFILE_SCOPE("Optimize Mesh");
      // meshopt_generateVertexRemap compares vertices byte by byte, so padding must not exist.
      static_assert(sizeof(Vertex) == 14 * sizeof(float), "Vertex must not have padding");

      // Merge vertices that are bitwise equal and remove vertices that no index uses.
      std::vector<u32> remap(vertices.size());
      size_t uniqueCount = meshopt_generateVertexRemap(remap.data(), indices.data(), indices.size(), vertices.data(),
                                                       vertices.size(), sizeof(Vertex));
      std::vector<Vertex> uniqueVertices(uniqueCount);
      meshopt_remapVertexBuffer(uniqueVertices.data(), vertices.data(), vertices.size(), sizeof(Vertex), remap.data());
      meshopt_remapIndexBuffer(indices.data(), indices.data(), indices.size(), remap.data());
      vertices = std::move(uniqueVertices);

      // Reorder triangles for the post-transform vertex cache.
      meshopt_optimizeVertexCache(indices.data(), indices.data(), indices.size(), vertices.size());

      // Reorder vertices into the order the index buffer first uses them.
      meshopt_optimizeVertexFetch(vertices.data(), indices.data(), indices.size(), vertices.data(), vertices.size(),
                                  sizeof(Vertex));
    }


    std::string writeToStore(ren::AssetStore &store) {
      json meta = json::object();
      meta["name"] = name;
      meta["type"] = "mesh";


      ren::Bundle bundle;

      bundle.setName(name);
      bundle.setKey("numVertices", vertices.size());
      bundle.setKey("numIndices", indices.size());
      bundle.attachBlob("vertices", vertices);
      bundle.attachBlob("indices", indices);

      std::vector<u8> data = bundle.serialize();

      return store.storeBlob(data, meta, "mesh-" + name).unwrap();
    }
  };

  void AssetManager::importMesh(const std::filesystem::path &filename) {
    ren::println("\n\n\nImporting mesh from path: {}", filename.string());


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
      return;
    }


    std::string sceneName = scene->mName.C_Str();

    std::vector<MeshImport> meshes;
    meshes.reserve(scene->mNumMeshes);



    for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
      REN_PROFILE_SCOPE("Read Assimp Mesh");
      const aiMesh *assimpMesh = scene->mMeshes[i];

      auto &mesh = meshes.emplace_back();
      mesh.name = assimpMesh->mName.C_Str();

      mesh.vertices.reserve(assimpMesh->mNumVertices);


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
        mesh.vertices.push_back(vertex);
      }

      mesh.indices.reserve(assimpMesh->mNumFaces * 3);  // Assuming triangles
      for (unsigned int j = 0; j < assimpMesh->mNumFaces; ++j) {
        const aiFace &face = assimpMesh->mFaces[j];
        if (face.mNumIndices == 3) {  // Only triangles
          mesh.indices.push_back(face.mIndices[0]);
          mesh.indices.push_back(face.mIndices[1]);
          mesh.indices.push_back(face.mIndices[2]);
        } else {
          fmt::print("Warning: Non-triangle face found in mesh '{}', skipping face.\n", assimpMesh->mName.C_Str());
        }
      }

    }

    ren::parallel_for(meshes.size(), [&meshes](size_t i) {
      meshes[i].process();
    });

    for (auto &mesh : meshes) {
      mesh.writeToStore(getStore());
    }
  }
}  // namespace ren