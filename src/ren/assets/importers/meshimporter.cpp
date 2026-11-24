#include <ren/types.h>
#include "MeshImporter.h"
#include "ImportSource.h"
#include <ren/assets/DerivedAsset.h>
#include <ren/assets/Material.h>
#include <ren/assets/NativeAsset.h>
#include <ren/assets/Vertex.h>
#include <ren/core/ThreadPool.h>

#include <assimp/GltfMaterial.h>
#include <assimp/Importer.hpp>
#include <assimp/IOStream.hpp>
#include <assimp/IOSystem.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <meshoptimizer/meshoptimizer.h>

#include <exception>
#include <limits>
#include <map>
#include <set>

namespace ren {



  //=========================
  // Assimp IO
  //=========================

  // Adapt the source file interface to Assimp's reader.
  class MeshImporter::Stream : public Assimp::IOStream {
    ref<AssetFile> file;
    size_t length;

   public:
    Stream(ref<AssetFile> file, size_t length)
        : file(std::move(file))
        , length(length) {}

    size_t Read(void *buffer, size_t size, size_t count) override {
      if (size == 0) {
        return 0;
      }
      count = std::min(count, (length - size_t(file->tell())) / size);
      return file->read(ByteSpan(buffer, count * size)) / size;
    }

    size_t Write(const void *, size_t, size_t) override { return 0; }

    aiReturn Seek(size_t offset, aiOrigin origin) override {
      size_t base = 0;
      if (origin == aiOrigin_CUR) {
        base = file->tell();
      } else if (origin == aiOrigin_END) {
        base = length;
      } else if (origin != aiOrigin_SET) {
        return aiReturn_FAILURE;
      }

      // Assimp encodes negative relative offsets in the unsigned offset argument.
      size_t destination;
      if (offset > size_t(std::numeric_limits<int64_t>::max())) {
        size_t backward = size_t(0) - offset;
        if (backward > base) {
          return aiReturn_FAILURE;
        }
        destination = base - backward;
      } else {
        if (offset > length - base) {
          return aiReturn_FAILURE;
        }
        destination = base + offset;
      }
      return file->seek(destination) ? aiReturn_SUCCESS : aiReturn_FAILURE;
    }

    size_t Tell() const override { return file->tell(); }
    size_t FileSize() const override { return length; }
    void Flush() override {}
  };


  // Give Assimp read access to the asset sources only. Each opened file becomes a tracked import input.
  class MeshImporter::IO : public Assimp::IOSystem {
    ImportContext &context;

   public:
    explicit IO(ImportContext &context)
        : context(context) {}

    bool Exists(const char *path) const override {
      auto name = import_detail::trySourcePath(path);
      return name && context.source.isFile(name.unwrap().generic_string());
    }

    char getOsSeparator() const override { return '/'; }
    void Close(Assimp::IOStream *stream) override { delete stream; }

    bool ComparePaths(const char *a, const char *b) const override {
      auto first = import_detail::trySourcePath(a);
      auto second = import_detail::trySourcePath(b);
      return first && second && first.unwrap() == second.unwrap();
    }

    Assimp::IOStream *Open(const char *path, const char *mode = "rb") override {
      if (mode[0] != 'r' || std::strchr(mode, '+')) {
        return nullptr;
      }

      auto name = import_detail::trySourcePath(path);
      if (!name || !context.source.isFile(name.unwrap().generic_string())) {
        return nullptr;
      }

      auto file = context.openInput(name.unwrap());
      AssetStat stat;
      if (!file->stat(stat) || stat.size > std::numeric_limits<size_t>::max()) {
        return nullptr;
      }
      return new Stream(std::move(file), stat.size);
    }
  };



  //=========================
  // Import
  //=========================

  void MeshImporter::import(ImportContext &importContext) {
    context = &importContext;
    const auto &path = context->sourcePath;

    // The Assimp importer owns and deletes the IO handler. It also owns the scene, so `scene` is
    // valid only until this function returns.
    Assimp::Importer importer;
    importer.SetIOHandler(new IO(*context));
    unsigned flags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_CalcTangentSpace | aiProcess_FindInstances;
    if (options.flipUVs) {
      flags |= aiProcess_FlipUVs;
    }

    scene = importer.ReadFile(path.string(), flags);
    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
      throw std::runtime_error("Cannot import " + path.string() + ": " + importer.GetErrorString());
    }
    if (scene->HasAnimations()) {
      context->warn("Animations are not imported.");
    }

    importMeshes();
    importTextures();
    importMaterials();
  }


  // ren::parallel_for does not pass worker exceptions to the caller. Keep the first one and rethrow it here.
  template <typename Fn>
  void MeshImporter::parallelImport(size_t count, Fn &&fn) {
    std::mutex mutex;
    std::exception_ptr failure;
    ren::parallel_for(count, [&](size_t i) {
      try {
        fn(i);
      } catch (...) {
        std::scoped_lock lock(mutex);
        if (!failure) {
          failure = std::current_exception();
        }
      }
    });

    if (failure) {
      std::rethrow_exception(failure);
    }
  }



  //=========================
  // Geometry
  //=========================

  // Merge equal vertices, then reorder triangles and vertices for the GPU caches.
  void MeshImporter::optimizeMesh(MeshAsset &mesh) {
    REN_PROFILE_SCOPE("Optimize Mesh");
    auto &vertices = mesh.vertices;
    auto &indices = mesh.indices;

    // meshopt_generateVertexRemap compares vertices byte by byte, so padding must not exist.
    static_assert(sizeof(Vertex) == 14 * sizeof(float), "Vertex must not have padding");

    // Merge vertices that are bitwise equal and remove vertices that no index uses.
    std::vector<u32> remap(vertices.size());
    size_t uniqueCount = meshopt_generateVertexRemap(remap.data(), indices.data(), indices.size(), vertices.data(), vertices.size(), sizeof(Vertex));
    std::vector<Vertex> uniqueVertices(uniqueCount);
    meshopt_remapVertexBuffer(uniqueVertices.data(), vertices.data(), vertices.size(), sizeof(Vertex), remap.data());
    meshopt_remapIndexBuffer(indices.data(), indices.data(), indices.size(), remap.data());
    vertices = std::move(uniqueVertices);

    // Reorder triangles for the post-transform vertex cache.
    meshopt_optimizeVertexCache(indices.data(), indices.data(), indices.size(), vertices.size());

    // Reorder vertices into the order the index buffer first uses them.
    meshopt_optimizeVertexFetch(vertices.data(), indices.data(), indices.size(), vertices.data(), vertices.size(), sizeof(Vertex));
  }


  Vertex MeshImporter::readVertex(const aiMesh &source, u32 i) {
    Vertex vertex;
    vertex.pos = {source.mVertices[i].x, source.mVertices[i].y, source.mVertices[i].z};
    if (source.HasNormals()) {
      glm::vec3 normal(source.mNormals[i].x, source.mNormals[i].y, source.mNormals[i].z);
      if (glm::dot(normal, normal) > 0.0f) {
        vertex.normal = glm::normalize(normal);
      }
    }
    if (source.HasTangentsAndBitangents()) {
      vertex.tangent = {source.mTangents[i].x, source.mTangents[i].y, source.mTangents[i].z};
      vertex.bitangent = {source.mBitangents[i].x, source.mBitangents[i].y, source.mBitangents[i].z};
    }
    if (source.HasTextureCoords(0)) {
      vertex.texCoord = {source.mTextureCoords[0][i].x, source.mTextureCoords[0][i].y};
    }
    return vertex;
  }


  // Copy, optimize, and bake one Assimp mesh. Throws if the mesh is not a valid triangle list.
  MeshAsset MeshImporter::importGeometry(const aiMesh &source) {
    std::string name = source.mName.C_Str();
    if (!source.HasPositions() || source.mNumVertices == 0 || source.mNumFaces == 0) {
      throw std::runtime_error("Mesh has no triangle geometry: " + name);
    }

    MeshAsset mesh;
    mesh.vertices.reserve(source.mNumVertices);
    for (u32 i = 0; i < source.mNumVertices; ++i) {
      mesh.vertices.push_back(readVertex(source, i));
    }

    mesh.indices.reserve(size_t(source.mNumFaces) * 3);
    for (u32 i = 0; i < source.mNumFaces; ++i) {
      const auto &face = source.mFaces[i];
      if (face.mNumIndices != 3) {
        throw std::runtime_error("Mesh contains a non-triangle face: " + name);
      }
      for (u32 j = 0; j < 3; ++j) {
        if (face.mIndices[j] >= mesh.vertices.size()) {
          throw std::runtime_error("Mesh index is out of range: " + name);
        }
        mesh.indices.push_back(face.mIndices[j]);
      }
    }

    // The BVH triangle entries index the final index buffer, so the build comes after optimization.
    optimizeMesh(mesh);
    mesh.bvh = buildMeshBVH(mesh.vertices, mesh.indices);
    return mesh;
  }


  // One Assimp mesh of the scene.
  struct MeshImporter::MeshSource {
    // The index in aiScene::mMeshes. Nodes refer to the mesh by this index.
    u32 index = 0;
    // The output name. A mesh takes its name from the first node that uses it: "<node>/<k>", where k
    // is the index of the mesh in that node. Blender writes object names to nodes and splits an
    // object into one mesh per used material slot, so a rename in Blender changes the ID and an
    // added object or material does not. A second node with the same name becomes "<node>#1". A
    // mesh that no node uses is "mesh:<mesh name>#<index>".
    std::string localName;
    const aiMesh *mesh = nullptr;
  };


  // List every mesh of the scene, in index order, with its local name.
  std::vector<MeshImporter::MeshSource> MeshImporter::collectMeshes(void) const {
    std::vector<std::string> names(scene->mNumMeshes);
    std::map<std::string, u32> nodeNames;

    // Depth-first in child order, so that the first node to use a mesh is the same on each import.
    std::vector<const aiNode *> stack = {scene->mRootNode};
    while (!stack.empty()) {
      const auto *node = stack.back();
      stack.pop_back();
      for (u32 i = node->mNumChildren; i > 0; --i) {
        stack.push_back(node->mChildren[i - 1]);
      }
      if (node->mNumMeshes == 0) {
        continue;
      }

      std::string nodeName = node->mName.C_Str();
      auto count = nodeNames[nodeName]++;
      if (count > 0) {
        nodeName += "#" + std::to_string(count);
      }
      for (u32 k = 0; k < node->mNumMeshes; ++k) {
        auto &name = names.at(node->mMeshes[k]);
        if (name.empty()) {
          name = nodeName + "/" + std::to_string(k);
        }
      }
    }

    std::vector<MeshSource> sources;
    sources.reserve(scene->mNumMeshes);
    for (u32 i = 0; i < scene->mNumMeshes; ++i) {
      const auto *mesh = scene->mMeshes[i];
      if (names[i].empty()) {
        names[i] = "mesh:" + std::string(mesh->mName.C_Str()) + "#" + std::to_string(i);
      }
      sources.push_back({.index = i, .localName = std::move(names[i]), .mesh = mesh});
    }
    return sources;
  }


  void MeshImporter::importMeshes(void) {
    auto sources = collectMeshes();
    parallelImport(sources.size(), [&](size_t i) {
      const auto &mesh = *sources[i].mesh;
      const auto &name = sources[i].localName;
      if (!(mesh.mPrimitiveTypes & aiPrimitiveType_TRIANGLE)) {
        context->warn("Skipped non-triangle mesh: " + name);
        return;
      }
      if (mesh.HasBones() || mesh.mNumAnimMeshes != 0) {
        context->warn("Imported static geometry only: " + name);
      }

      auto asset = importGeometry(mesh);
      asset.name = name;
      context->writeDerived(asset, name);
    });
  }



  //=========================
  // Textures
  //=========================

  // One image of the scene, and the formats that its material slots need.
  struct MeshImporter::Image {
    // Null for an external file. The image name is then its source path.
    const aiTexture *embedded = nullptr;
    std::set<PixelFormat> formats;
  };


  // Color slots hold sRGB values. Other slots hold data, such as normals or roughness, that must stay linear.
  PixelFormat MeshImporter::getSlotFormat(int textureType) {
    switch (textureType) {
      case aiTextureType_DIFFUSE:
      case aiTextureType_BASE_COLOR:
      case aiTextureType_EMISSIVE:
      case aiTextureType_EMISSION_COLOR:
      case aiTextureType_SPECULAR:
        return PixelFormat::RGBA8_SRGB;
      default:
        return PixelFormat::RGBA8_UNORM;
    }
  }


  // The name of the image that a material texture reference names: "*<index>" for an embedded
  // image, or the source path for an external file. Assimp finds an embedded image by index or by
  // file name. Other references are paths relative to the source file.
  std::string MeshImporter::getImageName(const char *reference) const {
    auto [embedded, index] = scene->GetEmbeddedTextureAndIndex(reference);
    if (embedded) {
      return "*" + std::to_string(index);
    }
    return import_detail::sourcePath(context->sourcePath.parent_path() / reference).generic_string();
  }


  // The local name of the texture output of an image in one format. The image name is "*<index>"
  // or the image path, so "<image>/<format>" is unique.
  std::string MeshImporter::getTextureName(const std::string &image, PixelFormat format) {
    return image + (format == PixelFormat::RGBA8_SRGB ? "/sRGB" : "/linear");
  }


  // List the images of the scene by name. Material references to the same image share one entry.
  std::map<std::string, MeshImporter::Image> MeshImporter::collectImages(void) const {
    std::map<std::string, Image> images;

    for (u32 m = 0; m < scene->mNumMaterials; ++m) {
      const auto &material = *scene->mMaterials[m];
      for (int type = aiTextureType_DIFFUSE; type <= AI_TEXTURE_TYPE_MAX; ++type) {
        for (u32 i = 0; i < material.GetTextureCount(aiTextureType(type)); ++i) {
          aiString reference;
          if (material.GetTexture(aiTextureType(type), i, &reference) != aiReturn_SUCCESS) {
            continue;
          }

          auto &image = images[getImageName(reference.C_Str())];
          image.embedded = scene->GetEmbeddedTexture(reference.C_Str());
          image.formats.insert(getSlotFormat(type));
        }
      }
    }

    // Embedded images that no material uses become sRGB textures, so that assetman can show them.
    for (u32 i = 0; i < scene->mNumTextures; ++i) {
      auto &image = images["*" + std::to_string(i)];
      if (image.formats.empty()) {
        image.embedded = scene->mTextures[i];
        image.formats.insert(PixelFormat::RGBA8_SRGB);
      }
    }
    return images;
  }


  // Decode one image to RGBA8 pixels. The caller sets the pixel format.
  ref<TextureAsset> MeshImporter::decodeImage(const std::string &name, const Image &image) const {
    if (!image.embedded) {
      auto bytes = context->readInput(name);
      return TextureAsset::decode(bytes.data(), bytes.size(), PixelFormat::RGBA8_SRGB, name);
    }

    // An embedded image with no height is an encoded file of mWidth bytes.
    const auto &embedded = *image.embedded;
    if (embedded.mHeight == 0) {
      return TextureAsset::decode(embedded.pcData, embedded.mWidth, PixelFormat::RGBA8_SRGB, name);
    }

    // An embedded image with a height is raw texels.
    auto texture = make<TextureAsset>();
    texture->width = embedded.mWidth;
    texture->height = embedded.mHeight;
    size_t count = size_t(texture->width) * texture->height;
    texture->pixels.reserve(count * 4);
    for (size_t i = 0; i < count; ++i) {
      const auto &texel = embedded.pcData[i];
      texture->pixels.insert(texture->pixels.end(), {texel.r, texel.g, texel.b, texel.a});
    }
    return texture;
  }


  // Write one bundle for each format of each image. Each write gives the asset the ID of its local
  // name, so one asset serves all formats and the pixels are not copied.
  void MeshImporter::importTextures(void) {
    auto collected = collectImages();
    std::vector<std::pair<std::string, Image>> images(collected.begin(), collected.end());

    parallelImport(images.size(), [&](size_t i) {
      const auto &[name, image] = images[i];
      auto texture = decodeImage(name, image);

      for (auto format : image.formats) {
        bool srgb = format == PixelFormat::RGBA8_SRGB;
        texture->pixelFormat = format;
        texture->name = name + (srgb ? " (sRGB)" : " (linear)");
        context->writeDerived(*texture, getTextureName(name, format));
      }
    });
  }




  //=========================
  // Materials
  //=========================

  // One Assimp material that a mesh uses.
  struct MeshImporter::MaterialSource {
    // The index in aiScene::mMaterials. Meshes refer to the material by this index.
    u32 index = 0;
    // The output name: the Assimp material name, or "material#<index>" without one. A second
    // material with the same name becomes "<name>#1".
    std::string localName;
    const aiMaterial *material = nullptr;
  };


  // List the materials that a mesh uses, in index order. Assimp adds a default material that only
  // meshes without a material use, so an unused material is not listed.
  std::vector<MeshImporter::MaterialSource> MeshImporter::collectMaterials(void) const {
    std::set<u32> used;
    for (u32 i = 0; i < scene->mNumMeshes; ++i) {
      used.insert(scene->mMeshes[i]->mMaterialIndex);
    }

    // Name every material, used or not, so that a material's name does not depend on which
    // other materials the meshes use.
    std::vector<MaterialSource> sources;
    std::map<std::string, u32> counts;
    for (u32 i = 0; i < scene->mNumMaterials; ++i) {
      const auto *material = scene->mMaterials[i];
      std::string name = material->GetName().C_Str();
      if (name.empty()) {
        name = "material#" + std::to_string(i);
      }
      auto count = counts[name]++;
      if (count > 0) {
        name += "#" + std::to_string(count);
      }

      if (used.contains(i)) {
        sources.push_back({.index = i, .localName = std::move(name), .material = material});
      }
    }
    return sources;
  }


  // The PBR texture field that reads each Assimp texture slot.
  static constexpr std::pair<aiTextureType, const char *> kTextureSlots[] = {
      {aiTextureType_DIFFUSE, "baseColorTexture"}, {aiTextureType_DIFFUSE_ROUGHNESS, "metallicRoughnessTexture"},
      {aiTextureType_NORMALS, "normalTexture"},    {aiTextureType_LIGHTMAP, "occlusionTexture"},
      {aiTextureType_EMISSIVE, "emissiveTexture"},
  };


  // Make a PBR material from the factors and textures of an Assimp material. A texture field
  // refers to the texture output of its image, by the derived ID that importTextures() writes it
  // under, so the material and the texture can be written in any order.
  ref<Material> MeshImporter::makeMaterial(const MaterialSource &source) const {
    const auto &input = *source.material;
    auto material = make<Material>("ren/core/pbr");

    // The material data starts as zeros. Set defaults, so that a key that the file does not have
    // does not give a black or mirror-like material. The glTF importer always writes the factors,
    // so these apply to other formats. Metallic is 0, not the glTF default of 1, because such
    // files are mostly not metal.
    material->setColor("baseColor", glm::vec4(1.0f));
    material->setColor("emissiveFactor", glm::vec4(0.0f));
    material->setFloat("metallic", 0.0f);
    material->setFloat("roughness", 1.0f);
    material->setFloat("occlusionStrength", 1.0f);

    aiColor4D color;
    if (input.Get(AI_MATKEY_COLOR_DIFFUSE, color) == aiReturn_SUCCESS) {
      material->setColor("baseColor", glm::vec4(color.r, color.g, color.b, color.a));
    }
    if (input.Get(AI_MATKEY_COLOR_EMISSIVE, color) == aiReturn_SUCCESS) {
      material->setColor("emissiveFactor", glm::vec4(color.r, color.g, color.b, color.a));
    }

    float factor = 0.0f;
    if (input.Get(AI_MATKEY_METALLIC_FACTOR, factor) == aiReturn_SUCCESS) {
      material->setFloat("metallic", glm::clamp(factor, 0.0f, 1.0f));
    }
    if (input.Get(AI_MATKEY_ROUGHNESS_FACTOR, factor) == aiReturn_SUCCESS) {
      material->setFloat("roughness", glm::clamp(factor, 0.0f, 1.0f));
    }
    if (input.Get(AI_MATKEY_GLTF_TEXTURE_STRENGTH(aiTextureType_LIGHTMAP, 0), factor) == aiReturn_SUCCESS) {
      material->setFloat("occlusionStrength", glm::clamp(factor, 0.0f, 1.0f));
    }

    int twoSided = 0;
    if (input.Get(AI_MATKEY_TWOSIDED, twoSided) == aiReturn_SUCCESS && twoSided) {
      material->getPSO().cullMode = CullMode::None;
    }

    // collectImages() gives each image a format for each slot that uses it, so this texture output exists.
    for (const auto &[type, field] : kTextureSlots) {
      aiString reference;
      if (input.GetTexture(type, 0, &reference) != aiReturn_SUCCESS) {
        continue;
      }
      auto name = getTextureName(getImageName(reference.C_Str()), getSlotFormat(type));
      material->setTexture(field, context->derivedID(AssetType::Texture, name));
    }
    return material;
  }


  // Write one derived material for each material that a mesh uses. The materials are made one at
  // a time, because each one takes the shader cache lock.
  void MeshImporter::importMaterials(void) {
    for (const auto &source : collectMaterials()) {
      auto material = makeMaterial(source);
      NativeContentAsset asset(AssetType::Material, material->serializeContent());
      asset.name = source.localName;
      context->writeDerived(asset, source.localName);
    }
  }

}  // namespace ren
