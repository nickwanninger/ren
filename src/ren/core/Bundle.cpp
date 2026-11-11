#include <ren/core/Bundle.h>
#include <fstream>
#include "ren/core/Logging.h"

namespace ren {


  Bundle::Bundle(std::span<const u8> data) {
    if (data.size() < sizeof(u32)) {
      throw std::runtime_error("Data too small to contain bundle header");
    }

    u32 jsonSize;
    std::memcpy(&jsonSize, data.data(), sizeof(u32));

    if (data.size() - sizeof(u32) < jsonSize) {
      throw std::runtime_error("Data too small to contain JSON metadata");
    }

    auto jsonBegin = data.begin() + sizeof(u32);
    auto j = json::from_msgpack(jsonBegin, jsonBegin + jsonSize);

    u32 version = j.at("version").get<u32>();
    if (version != VERSION) {
      throw std::runtime_error(fmt::format("Unsupported bundle version {} (expected {})", version, VERSION));
    }

    name = j.at("name").get<std::string>();
    metadata = j.at("metadata");

    // Blob offsets are relative to the start of the blob region, which follows the JSON.
    const u64 blobBase = sizeof(u32) + jsonSize;
    const u64 blobRegionSize = data.size() - blobBase;

    for (const auto &jb : j.at("blobs")) {
      Blob blob;
      blob.name = jb.at("name").get<std::string>();
      u64 offset = jb.at("offset").get<u64>();
      u64 size = jb.at("size").get<u64>();

      if (offset > blobRegionSize || size > blobRegionSize - offset) {
        throw std::runtime_error("Data too small to contain blob: " + blob.name);
      }

      blob.data.resize(size);
      std::memcpy(blob.data.data(), data.data() + blobBase + offset, size);
      blobs.push_back(std::move(blob));
    }
  }

  u32 Bundle::attachBlob(const std::string_view &name, const std::span<u8> &data) {
    Blob blob;
    blob.name = name;
    blob.data.resize(data.size());
    std::memcpy(blob.data.data(), data.data(), data.size());
    blobs.push_back(std::move(blob));
    return static_cast<u32>(blobs.size() - 1);
  }



  std::vector<u8> Bundle::serialize(void) {
    json j;
    j["version"] = VERSION;
    j["name"] = name;
    j["metadata"] = metadata;

    // An array keeps blobs in attach order, so indices from attachBlob stay valid after a load.
    j["blobs"] = json::array();
    u64 offset = 0;
    for (const auto &blob : blobs) {
      json jb;
      jb["name"] = blob.name;
      jb["size"] = blob.data.size();
      jb["offset"] = offset;
      offset += blob.data.size();
      // Data will be written separately
      j["blobs"].push_back(jb);
    }

    auto encodedJson = json::to_msgpack(j);
    u32 jsonSize = static_cast<u32>(encodedJson.size());

    std::vector<u8> output;
    output.resize(sizeof(u32) + jsonSize + offset);

    std::memcpy(output.data(), &jsonSize, sizeof(u32));
    std::memcpy(output.data() + sizeof(u32), encodedJson.data(), jsonSize);

    u64 currentOffset = sizeof(u32) + jsonSize;
    for (const auto &blob : blobs) {
      std::memcpy(output.data() + currentOffset, blob.data.data(), blob.data.size());
      currentOffset += blob.data.size();
    }

    return output;
  }

  void Bundle::write(const std::string_view &outPath) {
    // Ensure output directory exists
    std::filesystem::create_directories(std::filesystem::path(outPath).parent_path());


    auto serializedData = serialize();
    std::ofstream ofs(std::filesystem::path(outPath), std::ios::binary);
    ofs.write(reinterpret_cast<const char *>(serializedData.data()), serializedData.size());
    ofs.close();
  }

}  // namespace ren