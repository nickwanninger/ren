#include <ren/types.h>
#include "MeshImporter.h"
#include "ImportSource.h"
#include <ren/assets/DerivedAsset.h>
#include <ren/assets/Vertex.h>
#include <ren/core/ThreadPool.h>

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


  void MeshImporter::importMeshes(void) {
    parallelImport(scene->mNumMeshes, [&](size_t i) {
      const auto &mesh = *scene->mMeshes[i];
      std::string name = mesh.mName.C_Str();
      if (!(mesh.mPrimitiveTypes & aiPrimitiveType_TRIANGLE)) {
        context->warn("Skipped non-triangle mesh: " + name);
        return;
      }
      if (mesh.HasBones() || mesh.mNumAnimMeshes != 0) {
        context->warn("Imported static geometry only: " + name);
      }

      auto asset = importGeometry(mesh);
      asset.name = name.empty() ? "mesh_" + std::to_string(i) : name;
      context->writeDerived(asset);
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


  // List the images of the scene by name. A name is "*<index>" for an embedded image, or the
  // source path for an external file. Material references to the same image share one entry.
  std::map<std::string, MeshImporter::Image> MeshImporter::collectImages(void) const {
    std::map<std::string, Image> images;
    auto directory = context->sourcePath.parent_path();

    for (u32 m = 0; m < scene->mNumMaterials; ++m) {
      const auto &material = *scene->mMaterials[m];
      for (int type = aiTextureType_DIFFUSE; type <= AI_TEXTURE_TYPE_MAX; ++type) {
        for (u32 i = 0; i < material.GetTextureCount(aiTextureType(type)); ++i) {
          aiString reference;
          if (material.GetTexture(aiTextureType(type), i, &reference) != aiReturn_SUCCESS) {
            continue;
          }

          // Assimp finds an embedded image by index or by file name. Other references are paths
          // relative to the source file.
          auto [embedded, index] = scene->GetEmbeddedTextureAndIndex(reference.C_Str());
          auto name = embedded ? "*" + std::to_string(index) : import_detail::sourcePath(directory / reference.C_Str()).generic_string();

          auto &image = images[name];
          image.embedded = embedded;
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


  // Write one bundle for each format of each image. Each write gives the asset a fresh ID, so one
  // asset serves all formats and the pixels are not copied.
  void MeshImporter::importTextures(void) {
    auto collected = collectImages();
    std::vector<std::pair<std::string, Image>> images(collected.begin(), collected.end());

    parallelImport(images.size(), [&](size_t i) {
      const auto &[name, image] = images[i];
      auto texture = decodeImage(name, image);

      for (auto format : image.formats) {
        texture->pixelFormat = format;
        texture->name = name + (format == PixelFormat::RGBA8_SRGB ? " (sRGB)" : " (linear)");
        context->writeDerived(*texture);
      }
    });
  }

}  // namespace ren
