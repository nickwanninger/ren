#include <imgui.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <ren/renderer/Texture.h>

namespace ren {
  class AssimpSceneInspector {
   private:
    const aiScene* scene;
    std::unordered_map<const aiNode*, bool> nodeOpenState;
    std::unordered_map<const aiMesh*, bool> meshDetailState;
    std::unordered_map<const aiMaterial*, bool> materialDetailState;

    std::unordered_map<u32, ref<Texture>> textures;


    // Cache for expensive string operations
    mutable std::unordered_map<const void*, std::string> stringCache;

   public:
    AssimpSceneInspector(const aiScene* scene)
        : scene(scene) {}

    void render(const char* window_title = "Scene Inspector") {
      if (!scene) return;

      renderSceneInfo();
      ImGui::Separator();

      if (ImGui::CollapsingHeader("Scene Graph", ImGuiTreeNodeFlags_DefaultOpen)) {
        renderNode(scene->mRootNode, 0);
      }

      if (ImGui::CollapsingHeader("Meshes")) { renderMeshes(); }

      if (ImGui::CollapsingHeader("Materials")) { renderMaterials(); }

      if (ImGui::CollapsingHeader("Textures")) { renderTextures(); }

      if (ImGui::CollapsingHeader("Animations")) { renderAnimations(); }
    }

   private:
    void renderSceneInfo() {
      ImGui::Text("Scene Statistics:");
      ImGui::BulletText("Meshes: %u", scene->mNumMeshes);
      ImGui::BulletText("Materials: %u", scene->mNumMaterials);
      ImGui::BulletText("Textures: %u", scene->mNumTextures);
      ImGui::BulletText("Animations: %u", scene->mNumAnimations);
      ImGui::BulletText("Lights: %u", scene->mNumLights);
      ImGui::BulletText("Cameras: %u", scene->mNumCameras);

      // Scene flags
      if (scene->mFlags) {
        ImGui::Text("Flags: ");
        ImGui::SameLine();
        if (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) {
          ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "INCOMPLETE ");
        }
        if (scene->mFlags & AI_SCENE_FLAGS_VALIDATED) {
          ImGui::TextColored(ImVec4(0, 1, 0, 1), "VALIDATED ");
        }
        if (scene->mFlags & AI_SCENE_FLAGS_VALIDATION_WARNING) {
          ImGui::TextColored(ImVec4(1, 1, 0, 1), "VALIDATION_WARNING ");
        }
      }
    }

    void renderNode(const aiNode* node, int depth) {
      if (!node) return;

      ImGui::PushID(node);


      // Node header with expand/collapse
      bool& isOpen = nodeOpenState[node];
      const char* nodeName = node->mName.length > 0 ? node->mName.C_Str() : "<unnamed>";

      if (node->mNumChildren > 0) {
        isOpen = ImGui::TreeNodeEx(nodeName, ImGuiTreeNodeFlags_OpenOnArrow |
                                                 (isOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0));
      } else {
        ImGui::TreeNodeEx(nodeName, ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
        isOpen = false;
      }

      // Node details
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("Meshes: %u", node->mNumMeshes);
        ImGui::Text("Children: %u", node->mNumChildren);
        ImGui::EndTooltip();
      }

      // Show transformation matrix
      if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) { ImGui::OpenPopup("NodeDetails"); }

      if (ImGui::BeginPopup("NodeDetails")) {
        ImGui::Text("Node: %s", nodeName);
        ImGui::Separator();

        // Transformation matrix
        const aiMatrix4x4& t = node->mTransformation;
        ImGui::Text("Transformation Matrix:");
        ImGui::Text("%.3f %.3f %.3f %.3f", t.a1, t.a2, t.a3, t.a4);
        ImGui::Text("%.3f %.3f %.3f %.3f", t.b1, t.b2, t.b3, t.b4);
        ImGui::Text("%.3f %.3f %.3f %.3f", t.c1, t.c2, t.c3, t.c4);
        ImGui::Text("%.3f %.3f %.3f %.3f", t.d1, t.d2, t.d3, t.d4);

        // Mesh indices
        if (node->mNumMeshes > 0) {
          ImGui::Text("Mesh Indices:");
          for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
            ImGui::Text("  [%u] -> Mesh %u", i, node->mMeshes[i]);
          }
        }

        ImGui::EndPopup();
      }

      // Render children
      if (isOpen && node->mNumChildren > 0) {
        for (unsigned int i = 0; i < node->mNumChildren; ++i) {
          renderNode(node->mChildren[i], depth + 1);
        }
        ImGui::TreePop();
      }


      ImGui::PopID();
    }

    void renderMeshes() {
      for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
        const aiMesh* mesh = scene->mMeshes[i];
        ImGui::PushID(mesh);

        const char* meshName = mesh->mName.length > 0 ? mesh->mName.C_Str() : "<unnamed>";
        bool& detailOpen = meshDetailState[mesh];

        if (ImGui::TreeNodeEx(meshName, detailOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
          detailOpen = true;

          ImGui::Text("Vertices: %u", mesh->mNumVertices);
          ImGui::Text("Faces: %u", mesh->mNumFaces);
          ImGui::Text("Material Index: %u", mesh->mMaterialIndex);
          ImGui::Text("Primitive Types: %s",
                      getPrimitiveTypesString(mesh->mPrimitiveTypes).c_str());

          // Vertex components
          ImGui::Text("Vertex Components:");
          if (mesh->HasPositions()) ImGui::BulletText("Positions");
          if (mesh->HasNormals()) ImGui::BulletText("Normals");
          if (mesh->HasTangentsAndBitangents()) ImGui::BulletText("Tangents & Bitangents");

          for (unsigned int j = 0; j < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++j) {
            if (mesh->HasTextureCoords(j)) {
              ImGui::BulletText("Texture Coords %u (%uD)", j, mesh->mNumUVComponents[j]);
            }
          }

          for (unsigned int j = 0; j < AI_MAX_NUMBER_OF_COLOR_SETS; ++j) {
            if (mesh->HasVertexColors(j)) { ImGui::BulletText("Vertex Colors %u", j); }
          }

          // Bones
          if (mesh->mNumBones > 0) {
            ImGui::Text("Bones: %u", mesh->mNumBones);
            if (ImGui::TreeNode("Bone List")) {
              for (unsigned int j = 0; j < mesh->mNumBones; ++j) {
                const aiBone* bone = mesh->mBones[j];
                ImGui::Text("[%u] %s (Weights: %u)", j, bone->mName.C_Str(), bone->mNumWeights);
              }
              ImGui::TreePop();
            }
          }

          ImGui::TreePop();
        } else {
          detailOpen = false;
        }

        ImGui::PopID();
      }
    }

    void renderMaterials() {
      for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
        const aiMaterial* material = scene->mMaterials[i];
        ImGui::PushID(material);

        aiString name;
        material->Get(AI_MATKEY_NAME, name);
        const char* matName = name.length > 0 ? name.C_Str() : "<unnamed>";

        bool& detailOpen = materialDetailState[material];
        if (ImGui::TreeNodeEx(matName, detailOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
          detailOpen = true;

          ImGui::Text("Properties: %u", material->mNumProperties);

          // Common material properties
          aiColor3D color;
          float value;

          if (material->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS) {
            ImGui::ColorEdit3("Diffuse", &color.r, ImGuiColorEditFlags_DisplayRGB);
          }

          if (material->Get(AI_MATKEY_COLOR_SPECULAR, color) == AI_SUCCESS) {
            ImGui::ColorEdit3("Specular", &color.r, ImGuiColorEditFlags_DisplayRGB);
          }

          if (material->Get(AI_MATKEY_COLOR_AMBIENT, color) == AI_SUCCESS) {
            ImGui::ColorEdit3("Ambient", &color.r, ImGuiColorEditFlags_DisplayRGB);
          }

          if (material->Get(AI_MATKEY_SHININESS, value) == AI_SUCCESS) {
            ImGui::Text("Shininess: %.2f", value);
          }

          if (material->Get(AI_MATKEY_OPACITY, value) == AI_SUCCESS) {
            ImGui::Text("Opacity: %.2f", value);
          }


          // Texture counts
          for (int type = 0; type < aiTextureType_UNKNOWN; ++type) {
            unsigned int count = material->GetTextureCount(static_cast<aiTextureType>(type));
            if (count > 0) {
              ImGui::Text("texture - %d %s: %u", type, getTextureTypeName(type), count);
              // show the textures of this type

              if (ImGui::TreeNode(getTextureTypeName(type))) {
                for (unsigned int j = 0; j < count; ++j) {
                  aiString texturePath;
                  aiReturn ret =
                      material->GetTexture(static_cast<aiTextureType>(type), j, &texturePath);
                  if (ret == AI_SUCCESS) {
                    ImGui::Text("[%u] %s", j, texturePath.C_Str());
                  } else {
                    ImGui::Text("[%u] <error retrieving texture>", j);
                  }
                }
                ImGui::TreePop();
              }
            }
          }

          ImGui::TreePop();
        } else {
          detailOpen = false;
        }

        ImGui::PopID();
      }
    }

    void renderTextures() {
      for (unsigned int i = 0; i < scene->mNumTextures; ++i) {
        const aiTexture* texture = scene->mTextures[i];

        ImGui::PushID(texture);

        const char* filename =
            texture->mFilename.length > 0 ? texture->mFilename.C_Str() : "<embedded>";

        if (ImGui::TreeNode(filename)) {
          auto it = textures.find(i);
          if (it == textures.end()) {
            if (texture->mHeight == 0) {
              textures[i] = ren::Texture::load("embedded", texture->pcData, texture->mWidth);
            } else {
              textures[i] = ren::Texture::load(texture->mFilename.C_Str());
            }
          }

          if (texture->mHeight == 0) {
            ImGui::Text("Compressed texture");
            ImGui::Text("Format hint: %s", texture->achFormatHint);
            ImGui::Text("Data size: %u bytes", texture->mWidth);
          } else {
            ImGui::Text("Uncompressed texture");
            ImGui::Text("Dimensions: %u x %u", texture->mWidth, texture->mHeight);
            ImGui::Text("Format hint: %s", texture->achFormatHint);
          }


          ImGui::Image(textures[i]->getImGui(), ImVec2(512, 512));  // Flip Y for correct display

          ImGui::TreePop();
        }

        ImGui::PopID();
      }
    }

    void renderAnimations() {
      for (unsigned int i = 0; i < scene->mNumAnimations; ++i) {
        const aiAnimation* anim = scene->mAnimations[i];
        ImGui::PushID(anim);

        const char* animName = anim->mName.length > 0 ? anim->mName.C_Str() : "<unnamed>";

        if (ImGui::TreeNode(animName)) {
          ImGui::Text("Duration: %.2f", anim->mDuration);
          ImGui::Text("Ticks per second: %.2f", anim->mTicksPerSecond);
          ImGui::Text("Channels: %u", anim->mNumChannels);
          ImGui::Text("Mesh channels: %u", anim->mNumMeshChannels);

          if (ImGui::TreeNode("Channels")) {
            for (unsigned int j = 0; j < anim->mNumChannels; ++j) {
              const aiNodeAnim* channel = anim->mChannels[j];
              ImGui::Text("[%u] %s", j, channel->mNodeName.C_Str());
              ImGui::SameLine();
              ImGui::Text("(P:%u R:%u S:%u)", channel->mNumPositionKeys, channel->mNumRotationKeys,
                          channel->mNumScalingKeys);
            }
            ImGui::TreePop();
          }

          ImGui::TreePop();
        }

        ImGui::PopID();
      }
    }

    std::string getPrimitiveTypesString(unsigned int types) {
      std::string result;
      if (types & aiPrimitiveType_POINT) result += "POINT ";
      if (types & aiPrimitiveType_LINE) result += "LINE ";
      if (types & aiPrimitiveType_TRIANGLE) result += "TRIANGLE ";
      if (types & aiPrimitiveType_POLYGON) result += "POLYGON ";
      return result.empty() ? "NONE" : result;
    }

    const char* getTextureTypeName(int type) {
      switch (type) {
        case aiTextureType_DIFFUSE: return "Diffuse";
        case aiTextureType_SPECULAR: return "Specular";
        case aiTextureType_AMBIENT: return "Ambient";
        case aiTextureType_EMISSIVE: return "Emissive";
        case aiTextureType_HEIGHT: return "Height";
        case aiTextureType_NORMALS: return "Normals";
        case aiTextureType_SHININESS: return "Shininess";
        case aiTextureType_OPACITY: return "Opacity";
        case aiTextureType_DISPLACEMENT: return "Displacement";
        case aiTextureType_LIGHTMAP: return "Lightmap";
        case aiTextureType_REFLECTION: return "Reflection";
        default: return "Unknown";
      }
    }
  };

}  // namespace ren