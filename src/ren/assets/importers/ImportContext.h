#pragma once

#include <ren/assets/DerivedAsset.h>
#include <map>
#include <mutex>

namespace ren {

  // One import operation. The manager owns settings and publication; importers read inputs and emit bundles.
  // The input, output, and warning calls are thread-safe.
  class ImportContext {
   public:
    AssetSource &source;
    const AssetID sourceAsset;
    const std::filesystem::path sourcePath;

    // Construct from validated source identity. Does not write the sidecar.
    ImportContext(AssetSource &source, AssetID sourceAsset, std::filesystem::path path);

    // Read input bytes and record their hash for the final import fingerprint.
    std::vector<u8> readInput(const std::filesystem::path &path);

    // Open an input for Assimp and record its hash. Throws if opening or hashing fails.
    ref<AssetFile> openInput(const std::filesystem::path &path);

    // Assign a fresh identity and ownership to the asset, write a bundle, and record the output.
    // Returns the new ID. A second call with the same asset writes a second bundle.
    AssetID writeDerived(Asset &asset);

    // Record an unsupported feature for display in assetman.
    void warn(std::string message);

    // Import results for the record. Read them only after the importer returns.
    const std::map<std::string, std::string> &getInputHashes(void) const { return inputHashes; }
    const json &getOutputs(void) const { return outputs; }
    const json &getWarnings(void) const { return warnings; }

   private:
    friend class AssetManager;

    // Protects the input hashes, outputs, and warnings.
    std::mutex mutex;
    std::map<std::string, std::string> inputHashes;
    json outputs = json::array();
    json warnings = json::array();

    void trackInput(const std::string &name, const std::string &hash);
    void validate() const;
  };

}  // namespace ren
