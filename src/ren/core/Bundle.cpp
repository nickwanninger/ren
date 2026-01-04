#include <ren/core/Bundle.h>
#include <fstream>
#include "ren/core/Logging.h"

namespace ren {



  u32 BundleBuilder::attachBlob(const std::span<u8> &data) {
    Blob blob;
    blob.data.resize(data.size());
    std::memcpy(blob.data.data(), data.data(), data.size());
    blobs.push_back(std::move(blob));
    return static_cast<u32>(blobs.size() - 1);
  }




  void BundleBuilder::write(const std::string_view &outPath) {
    // Ensure output directory exists
    std::filesystem::create_directories(std::filesystem::path(outPath).parent_path());

    json j;
    j["name"] = name;
    j["metadata"] = metadata;

    j["blobs"] = json::array();
    u64 offset = 0;
    for (const auto &blob : blobs) {
      json jb;
      jb["size"] = blob.data.size();
      jb["offset"] = offset;
      offset += blob.data.size();
      // Data will be written separately
      j["blobs"].push_back(jb);
    }


    std::ofstream ofs(std::filesystem::path(outPath), std::ios::binary);

    // Write JSON header
    ren::println("Bundle JSON: {}", j.dump(2));


    auto encodedJson = json::to_msgpack(j);
    u32 jsonSize = static_cast<u32>(encodedJson.size());
    ofs.write(reinterpret_cast<const char *>(&jsonSize), sizeof(u32));
    ofs.write(reinterpret_cast<const char*>(encodedJson.data()), jsonSize);

    // Write blobs
    for (const auto &blob : blobs) {
      ofs.write(reinterpret_cast<const char *>(blob.data.data()), blob.data.size());
    }

    ofs.close();
  }
}  // namespace ren