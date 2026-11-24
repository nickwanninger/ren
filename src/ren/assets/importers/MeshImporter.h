#pragma once

#include <ren/assets/MeshAsset.h>
#include <ren/assets/TextureAsset.h>
#include <ren/assets/importers/Importer.h>
#include <map>

struct aiMesh;
struct aiScene;

namespace ren {

  class Material;

  // Converts static geometry, baked BVHs, model textures, and materials through Assimp.
  // import() reads the scene, then writes one bundle per mesh, one per texture format of each image,
  // and one per material.
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

    struct MeshSource;
    struct Image;
    struct MaterialSource;

    Options options;

    // Valid only while import() runs.
    ImportContext *context = nullptr;
    const aiScene *scene = nullptr;

    // Run `fn(i)` for each index on the thread pool. Rethrows the first exception of a worker.
    template <typename Fn>
    static void parallelImport(size_t count, Fn &&fn);

    // Geometry
    void importMeshes(void);
    std::vector<MeshSource> collectMeshes(void) const;
    static MeshAsset importGeometry(const aiMesh &source);
    static Vertex readVertex(const aiMesh &source, u32 i);
    static void optimizeMesh(MeshAsset &mesh);

    // Textures
    void importTextures(void);
    std::map<std::string, Image> collectImages(void) const;
    ref<TextureAsset> decodeImage(const std::string &name, const Image &image) const;
    static PixelFormat getSlotFormat(int textureType);
    std::string getImageName(const char *reference) const;
    static std::string getTextureName(const std::string &image, PixelFormat format);

    // Materials
    void importMaterials(void);
    std::vector<MaterialSource> collectMaterials(void) const;
    static ref<Material> makeMaterial(const MaterialSource &source);
  };

}  // namespace ren
