#include <ren/assets/MeshScene.hpp>

#include <imgui.h>
#include <tinygltf/tiny_gltf.h>
#include <math.h>

namespace ren {


  static void convertMesh(MeshScene &scene, const tinygltf::Model &model,
                          const tinygltf::Mesh &mesh) {
    for (const auto &primitive : mesh.primitives) {
      REN_PROFILE_SCOPE("Read Primitive");
      std::vector<Vertex> vertices;
      std::vector<u32> indices;

      // dump the primitive material
      if (primitive.material >= 0) {
        const tinygltf::Material &material = model.materials[primitive.material];
        // You can also dump the material properties here if needed
      } else {
        fmt::print("  No material assigned to this primitive\n");
      }

      // if the mesh is triangles, loop over the vertices
      if (primitive.mode == TINYGLTF_MODE_TRIANGLES) {
        REN_PROFILE_SCOPE("Read triangle primitive");
        // Assuming the position attribute is present
        auto posIt = primitive.attributes.find("POSITION");
        if (posIt != primitive.attributes.end()) {
          REN_PROFILE_SCOPE("Read Positions");
          int accessorIndex = posIt->second;
          const tinygltf::Accessor &accessor = model.accessors[accessorIndex];
          const tinygltf::BufferView &bufferView = model.bufferViews[accessor.bufferView];
          const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];

          // Read the vertex positions
          const float *dataPtr =
              reinterpret_cast<const float *>(buffer.data.data() + bufferView.byteOffset);
          for (size_t i = 0; i < accessor.count; ++i) {
            Vertex vertex;
            vertex.pos.x = dataPtr[i * 3 + 0];
            vertex.pos.y = dataPtr[i * 3 + 1];
            vertex.pos.z = dataPtr[i * 3 + 2];
            vertices.push_back(vertex);
          }
        }


        // Get normals
        auto normalIt = primitive.attributes.find("NORMAL");
        if (normalIt != primitive.attributes.end()) {
          REN_PROFILE_SCOPE("Read Normals");
          int accessorIndex = normalIt->second;
          const tinygltf::Accessor &accessor = model.accessors[accessorIndex];
          const tinygltf::BufferView &bufferView = model.bufferViews[accessor.bufferView];
          const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];
          const float *dataPtr =
              reinterpret_cast<const float *>(buffer.data.data() + bufferView.byteOffset);
          for (size_t i = 0; i < accessor.count; ++i) {
            if (i < vertices.size()) {
              vertices[i].normal.x = dataPtr[i * 3 + 0];
              vertices[i].normal.y = dataPtr[i * 3 + 1];
              vertices[i].normal.z = dataPtr[i * 3 + 2];
            } else {
              fmt::print("Warning: More normals than vertices in GLTF file\n");
            }
          }
        }

        // Get texture coordinates
        auto texCoordIt = primitive.attributes.find("TEXCOORD_0");
        if (texCoordIt != primitive.attributes.end()) {
          REN_PROFILE_SCOPE("Read Texture Coordinates");
          int accessorIndex = texCoordIt->second;
          const tinygltf::Accessor &accessor = model.accessors[accessorIndex];
          const tinygltf::BufferView &bufferView = model.bufferViews[accessor.bufferView];
          const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];
          const float *dataPtr =
              reinterpret_cast<const float *>(buffer.data.data() + bufferView.byteOffset);
          for (size_t i = 0; i < accessor.count; ++i) {
            if (i < vertices.size()) {
              vertices[i].texCoord.x = dataPtr[i * 2 + 0];
              vertices[i].texCoord.y = dataPtr[i * 2 + 1];
            } else {
              fmt::print("Warning: More texture coordinates than vertices in GLTF file\n");
            }
          }
        }

        // Read indices if available
        if (primitive.indices >= 0) {
          REN_PROFILE_SCOPE("Read Indices");
          const auto &indexAccessor = model.accessors[primitive.indices];
          const auto &bufferView = model.bufferViews[indexAccessor.bufferView];
          const auto &buffer = model.buffers[bufferView.buffer];

          indices.reserve(indexAccessor.count);

          const uint8_t *bufferData =
              buffer.data.data() + bufferView.byteOffset + indexAccessor.byteOffset;

          // Handle different index formats
          switch (indexAccessor.componentType) {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
              const uint8_t *data = reinterpret_cast<const uint8_t *>(bufferData);
              for (size_t i = 0; i < indexAccessor.count; ++i) {
                indices.push_back(static_cast<uint32_t>(data[i]));
              }
              break;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
              const uint16_t *data = reinterpret_cast<const uint16_t *>(bufferData);
              for (size_t i = 0; i < indexAccessor.count; ++i) {
                indices.push_back(static_cast<uint32_t>(data[i]));
              }
              break;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
              const uint32_t *data = reinterpret_cast<const uint32_t *>(bufferData);
              indices.assign(data, data + indexAccessor.count);
              break;
            }
            default:
              // Invalid or unsupported index type
              throw std::runtime_error("Unsupported index component type: " +
                                       std::to_string(indexAccessor.componentType));
          }
        }

      } else {
        fmt::print("Skipping primitive with mode {} (not triangles)\n", primitive.mode);
      }
      scene.meshes.push_back(makeRef<Mesh>(mesh.name, vertices, indices));
    }
  }



  ref<MeshScene> MeshScene::loadGLTF(const std::string &filename) {
    REN_PROFILE_SCOPE("Load GLTF Mesh Scene");

    auto scene = makeRef<MeshScene>();

    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    loader.SetParseStrictness(tinygltf::ParseStrictness::Permissive);


    std::string err, warn;

    {
      REN_PROFILE_SCOPE("Load GLTF Binary");
      bool ret = loader.LoadBinaryFromFile(&model, &err, &warn, filename);
      if (!ret) {
        // try to load from text format instead.
        fmt::print("Failed to load GLTF file as binary: {}\n", err);
        fmt::print("Trying to load as text format...\n");
        ret = loader.LoadASCIIFromFile(&model, &err, &warn, filename);
        if (ret) {
          fmt::print("Loaded GLTF file as text format: {}\n", filename);
        } else {
          return {};
        }
        // return nullptr;
      }
    }


    for (const auto &mesh : model.meshes) {
      REN_PROFILE_SCOPE("Convert Mesh");
      convertMesh(*scene, model, mesh);
    }


    // Now grab all the nodes in the scene.
    for (const auto &node : model.nodes) {
      REN_PROFILE_SCOPE("Read Node");
      auto sceneNode = makeRef<MeshScene::Node>();
      sceneNode->name = node.name;

      // If the node has a mesh, add it to the scene.
      if (node.mesh >= 0 && node.mesh < model.meshes.size()) {
        sceneNode->mesh = scene->meshes[node.mesh];
      }




      // Read the transform
      if (node.translation.size() == 3) {
        sceneNode->transform.translation.x = node.translation[0];
        sceneNode->transform.translation.y = node.translation[1];
        sceneNode->transform.translation.z = node.translation[2];
      }

      if (node.rotation.size() == 4) {
        glm::quat rotation(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]);

        sceneNode->transform.rotation = rotation;
      }

      if (node.scale.size() == 3) {
        sceneNode->transform.scale.x = node.scale[0];
        sceneNode->transform.scale.y = node.scale[1];
        sceneNode->transform.scale.z = node.scale[2];
      }

      sceneNode->transform.transformMatrix = sceneNode->transform.getTransform();  // Update the transform matrix

      // Add the node to the scene
      scene->nodes.push_back(sceneNode);
    }


    // Now we need to build the hierarchy of nodes.
    int index = 0;
    for (const auto &node : model.nodes) {
      REN_PROFILE_SCOPE("Build Node Hierarchy");
      auto sceneNode = scene->nodes[index++];
      for (int childIndex : node.children) {
        if (childIndex >= 0 && childIndex < model.nodes.size()) {
          auto childNode = scene->nodes[childIndex];
          sceneNode->children.push_back(childNode);
        } else {
          fmt::print("Warning: Child index {} out of bounds for node {}\n", childIndex,
                     sceneNode->name);
        }
      }
    }

    // And finally, parse the main scene to produce a rootnode list.
    for (const auto &sceneNode : model.scenes) {
      REN_PROFILE_SCOPE("Parse Main Scene");
      for (int nodeIndex : sceneNode.nodes) {
        if (nodeIndex >= 0 && nodeIndex < model.nodes.size()) {
          auto node = scene->nodes[nodeIndex];
          // If the node has no parent, add it to the root nodes.
          if (std::find(scene->rootNodes.begin(), scene->rootNodes.end(), node) ==
              scene->rootNodes.end()) {
            scene->rootNodes.push_back(node);
          }
        } else {
          fmt::print("Warning: Node index {} out of bounds for scene\n", nodeIndex);
        }
      }
    }



    return scene;
  }


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

#define TEST(order)                                                      \
  {                                                                      \
    glm::extractEulerAngle##order(rotMatrix, euler.x, euler.y, euler.z); \
    printAngles(#order, euler);                                          \
  }
      TEST(XYZ)
      TEST(YXZ)
      TEST(XZX)
      TEST(XYX)
      TEST(YXY)
      TEST(YZY)
      TEST(ZYZ)
      TEST(ZXZ)
      TEST(XZY)
      TEST(YZX)
      TEST(ZYX)
      TEST(ZXY)
#undef TEST


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
    Entity entity = scene.createEntity(node.name);
    entity.get<comp::Transform>() = node.transform;  // Set the transform component.

    // If the node has a mesh, add it to the entity.
    if (node.mesh) { entity.add<comp::Mesh>(node.mesh); }

    // Add the entity as a child of the parent entity.
    if (parentEntity) parentEntity.addChild(entity);

    // Recursively instantiate child nodes.
    for (const auto &child : node.children) {
      instantiateNode(scene, *child, entity);
    }
  }

  Entity MeshScene::instantiate(ren::Scene &scene) {
    // Create an entity for each root node in the scene.
    Entity rootEntity = scene.createEntity();

    // Add the root nodes as children of the root entity.
    for (const auto &node : rootNodes) {
      instantiateNode(scene, *node, {});
    }

    return rootEntity;
  }
}  // namespace ren