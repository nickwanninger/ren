#pragma once

#include <ren/assets/MeshAsset.h>
#include <ren/assets/TextureAsset.h>
#include <ren/assets/importers/Importer.h>
#include <map>

struct aiMesh;
struct aiScene;

namespace ren {

  // Converts static geometry, baked BVHs, and model textures through Assimp.
  // import() reads the scene, then writes one bundle per mesh and one per texture format of each image.
  class MeshImporter : public Importer {
   public:
    struct Options {
      // Flip the V texture coordinate (aiProcess_FlipUVs).
      bool flipUVs = true;

      NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Options, flipUVs)
    };

    static std::vector<std::string> getExtensions(void) { return {".glb", ".gltf", ".fbx", ".obj"}; }

    explicit MeshImporter(Options options)
        : options(std::move(options)) {}

    void import(ImportContext &context) override;

   private:
    // Assimp file access through the asset sources.
    class Stream;
    class IO;

    struct Image;

    Options options;

    // Valid only while import() runs.
    ImportContext *context = nullptr;
    const aiScene *scene = nullptr;

    // Run `fn(i)` for each index on the thread pool. Rethrows the first exception of a worker.
    template <typename Fn>
    static void parallelImport(size_t count, Fn &&fn);

    // Geometry
    void importMeshes(void);
    static MeshAsset importGeometry(const aiMesh &source);
    static Vertex readVertex(const aiMesh &source, u32 i);
    static void optimizeMesh(MeshAsset &mesh);

    // Textures
    void importTextures(void);
    std::map<std::string, Image> collectImages(void) const;
    ref<TextureAsset> decodeImage(const std::string &name, const Image &image) const;
    static PixelFormat getSlotFormat(int textureType);
  };

}  // namespace ren
