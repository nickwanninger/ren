#include <ren/assets/Mesh.h>
#include <tinygltf/tiny_gltf.h>
#include <tinyobjloader/tiny_obj_loader.h>

namespace ren {


  Mesh::Mesh(const std::string &name, const std::vector<Vertex> &vertices,
             const std::vector<u32> &indices)
      : name(name)
      , vertices(vertices)
      , indices(indices) {
    REN_PROFILE_FUNCTION();

    // Create the vertex buffer.
    vertexBuffer = makeRef<VertexBuffer<Vertex>>(vertices);
    vertexBuffer->setName(fmt::format("Mesh: {} Vertex Buffer", name));

    // Create the index buffer.
    indexBuffer = makeRef<IndexBuffer>(indices);
    indexBuffer->setName(fmt::format("Mesh: {} Index Buffer", name));
  }

  Mesh::~Mesh() {}


  MeshRef createCubeMesh(void) { return ren::loadGLTF("assets/test/meshes/unit_cube.glb"); }

  MeshRef loadObj(const std::string &filename) {
    REN_PROFILE_FUNCTION();
    std::vector<Vertex> vertices;
    std::vector<u32> indices;
    tinyobj::ObjReaderConfig reader_config;
    // get the directory part of the filename. That's the search path for the .mtl files.
    std::string dir = filename.substr(0, filename.find_last_of("/\\"));
    reader_config.mtl_search_path = dir;
    tinyobj::ObjReader reader;
    fmt::print("\n\n");
    if (!reader.ParseFromFile(filename, reader_config)) {
      if (!reader.Error().empty()) {
        fmt::print("TinyObjReader: {}\n", reader.Error());
      } else {
        fmt::print("TinyObjReader: Unknown error\n");
      }
      return nullptr;
    }
    if (!reader.Warning().empty()) { fmt::print("TinyObjReader: {}\n", reader.Warning()); }

    fmt::print("Loaded OBJ file: {}\n", filename);
    const auto &attrib = reader.GetAttrib();
    const auto &shapes = reader.GetShapes();
    const auto &materials = reader.GetMaterials();
    fmt::print("Loaded OBJ model: {}\n", filename);
    fmt::print("Number of shapes: {}\n", shapes.size());
    fmt::print("Number of materials: {}\n", materials.size());


    // parse it into vertices and indices
    for (const auto &shape : shapes) {
      fmt::print("Shape name: {}\n", shape.name);
      fmt::print("Number of vertices: {}\n", shape.mesh.indices.size());
      fmt::print("Number of lines: {}\n", shape.lines.indices.size());
      fmt::print("Number of points: {}\n", shape.points.indices.size());
      for (const auto &index : shape.mesh.indices) {
        Vertex vertex;
        vertex.pos.x = attrib.vertices[3 * index.vertex_index + 0];
        vertex.pos.y = attrib.vertices[3 * index.vertex_index + 1];
        vertex.pos.z = attrib.vertices[3 * index.vertex_index + 2];

        if (index.normal_index >= 0) {
          vertex.normal.x = attrib.normals[3 * index.normal_index + 0];
          vertex.normal.y = attrib.normals[3 * index.normal_index + 1];
          vertex.normal.z = attrib.normals[3 * index.normal_index + 2];
        } else {
          vertex.normal = glm::vec3(0.0f, 0.0f, 0.0f);
        }

        if (index.texcoord_index >= 0) {
          vertex.texCoord.x = attrib.texcoords[2 * index.texcoord_index + 0];
          vertex.texCoord.y = attrib.texcoords[2 * index.texcoord_index + 1];
        } else {
          vertex.texCoord = glm::vec2(0.0f, 0.0f);
        }

        vertices.push_back(vertex);
        indices.push_back(static_cast<u32>(indices.size()));
      }
    }

    // for (auto &vert : vertices) {
    //   std::cout << (json)vert << std::endl;
    // }


    return makeRef<Mesh>(filename, vertices, indices);
  }

  MeshRef loadGLTF(const std::string &filename) {
    REN_PROFILE_FUNCTION();

    fmt::print("\n\n");
    fmt::print("loading GLTF file: {}\n", filename);

    // Load the GLTF file using tinygltf.
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    bool ret = loader.LoadBinaryFromFile(&model, &err, &warn, filename);
    if (!ret) {
      fmt::print("Failed to load GLTF file: {}\n", err);
      return nullptr;
    }

    // Process the model and create a MeshRef.
    // This is a simplified example; you would need to handle materials, textures, etc.
    std::vector<Vertex> vertices;
    std::vector<u32> indices;

    // Print out a bunch of information about the model.
    fmt::print("Loaded GLTF model: {}\n", filename);
    fmt::print("Number of meshes: {}\n", model.meshes.size());
    for (const auto &mesh : model.meshes) {
      fmt::print("Mesh name: {}\n", mesh.name);
      fmt::print("Number of primitives: {}\n", mesh.primitives.size());
      for (const auto &primitive : mesh.primitives) {
        fmt::print("  Primitive mode: {}\n", primitive.mode);
        fmt::print("  Number of attributes: {}\n", primitive.attributes.size());
        for (const auto &attr : primitive.attributes) {
          fmt::print("    Attribute: {}, {}\n", attr.first, attr.second);
        }
        if (primitive.indices >= 0) { fmt::print("  Indices: {}\n", primitive.indices); }

        // if the mesh is triangles, loop over the vertices
        if (primitive.mode == TINYGLTF_MODE_TRIANGLES) {
          // Assuming the position attribute is present
          auto posIt = primitive.attributes.find("POSITION");
          if (posIt != primitive.attributes.end()) {
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
                fmt::print("Warning: More normals than vertices in GLTF file: {}\n", filename);
              }
            }
          }

          // Get texture coordinates
          auto texCoordIt = primitive.attributes.find("TEXCOORD_0");
          if (texCoordIt != primitive.attributes.end()) {
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
                fmt::print("Warning: More texture coordinates than vertices in GLTF file: {}\n",
                           filename);
              }
            }
          }

          // Read indices if available
          if (primitive.indices >= 0) {
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
      }
    }

    // std::cout << (json)vertices << std::endl;
    // std::cout << (json)indices << std::endl;


    return makeRef<Mesh>(filename, vertices, indices);
  }


  static void convertMesh(std::vector<MeshRef> &outMeshes, const tinygltf::Model &model,
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
      outMeshes.push_back(makeRef<Mesh>(mesh.name, vertices, indices));
    }
  }



  std::vector<MeshRef> loadGLTFScene(const std::string &filename) {
    REN_PROFILE_FUNCTION();
    std::vector<MeshRef> meshes;


    fmt::print("loading GLTF file: {}\n", filename);

    // Load the GLTF file using tinygltf.
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
#if 1

    // first, dump out the material definitions
    fmt::print("Loaded GLTF model: {}\n", filename);
    fmt::print("Number of materials: {}\n", model.materials.size());
    for (const auto &material : model.materials) {
      fmt::print("Material name: {}\n", material.name);
      fmt::print("  PBR Metallic-Roughness:\n");
      fmt::print("    Base Color Factor: {}\n",
                 json(material.pbrMetallicRoughness.baseColorFactor).dump());
      fmt::print("    Metallic Factor: {}\n",
                 json(material.pbrMetallicRoughness.metallicFactor).dump());
      fmt::print("    Roughness Factor: {}\n",
                 json(material.pbrMetallicRoughness.roughnessFactor).dump());
      fmt::print("  Normal Texture: {}\n", material.normalTexture.index);
      fmt::print("  Occlusion Texture: {}\n", material.occlusionTexture.index);
      fmt::print("  Emissive Factor: {}\n", json(material.emissiveFactor).dump());
      fmt::print("  Emissive Texture: {}\n", material.emissiveTexture.index);
      fmt::print("  Alpha Mode: {}\n", material.alphaMode);
      fmt::print("  Alpha Cutoff: {}\n", material.alphaCutoff);
      fmt::print("  Double Sided: {}\n", material.doubleSided);

      // fmt::print("  Extensions: {}\n", (json)material.extensions.size());
      // fmt::print("  Extras: {}\n", material.extras.size());
    }

    // dump mesh information
    fmt::print("Number of meshes: {}\n", model.meshes.size());
    for (const auto &mesh : model.meshes) {
      fmt::print("Mesh name: {}\n", mesh.name);
      fmt::print("  Number of primitives: {}\n", mesh.primitives.size());
      for (const auto &primitive : mesh.primitives) {
        fmt::print("    Primitive mode: {}\n", primitive.mode);
        fmt::print("    Number of attributes: {}\n", primitive.attributes.size());
        for (const auto &attr : primitive.attributes) {
          fmt::print("      Attribute: {}, {}\n", attr.first, attr.second);
        }
        if (primitive.indices >= 0) {
          fmt::print("    Indices: {}\n", primitive.indices);
        } else {
          fmt::print("    No indices for this primitive\n");
        }
      }
    }

    // Dump nodes.
    fmt::print("Number of nodes: {}\n", model.nodes.size());
    for (const auto &node : model.nodes) {
      fmt::print("Node name: {}\n", node.name);
      fmt::print("  Mesh: {}\n", node.mesh);
      fmt::print("  Translation: {}\n", json(node.translation).dump());
      fmt::print("  Rotation: {}\n", json(node.rotation).dump());
      fmt::print("  Scale: {}\n", json(node.scale).dump());
      fmt::print("  Children: {}\n", json(node.children).dump());
      // fmt::print("  Extensions: {}\n", json(node.extensions).dump());
      // fmt::print("  Extras: {}\n", json(node.extras).dump());
    }




    // Dump scenes.
    fmt::print("Number of scenes: {}\n", model.scenes.size());
    for (const auto &scene : model.scenes) {
      fmt::print("Scene name: {}\n", scene.name);
      fmt::print("  Nodes: {}\n", json(scene.nodes).dump());
    }
#endif


    for (const auto &mesh : model.meshes) {
      REN_PROFILE_SCOPE("Convert Mesh");
      convertMesh(meshes, model, mesh);
    }

    // exit(0);


    return meshes;
  }
}  // namespace ren