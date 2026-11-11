#pragma once

#include <ren/types.h>
#include <cstring>

namespace ren {

  class Bundle {
   public:
    // Increment when the serialized layout changes.
    static constexpr u32 VERSION = 1;

    Bundle() = default;

    Bundle(std::span<const u8> data); // Deserialize a bundle from a binary blob.

    // No copy, no move
    Bundle(const Bundle &) = delete;
    Bundle &operator=(const Bundle &) = delete;
    Bundle(Bundle &&) = delete;
    Bundle &operator=(Bundle &&) = delete;

    struct Blob {
      std::string name;
      std::vector<u8> data;
    };

    void setName(const std::string_view &name) { this->name = name; }
    const std::string &getName(void) const { return name; }
    const json &getMetadata(void) const { return metadata; }
    const std::vector<Blob> &getBlobs(void) const { return blobs; }


    u32 attachBlob(const std::string_view &name, const std::span<u8> &data);
    template <typename T>
    u32 attachBlob(const std::string_view &name, const std::vector<T> &data) {
      return attachBlob(name, std::span<u8>((u8 *)data.data(), data.size() * sizeof(T)));
    }


    // Copy the blob `name` into a vector of T. Throws if the blob is absent, is present more than
    // one time, or has a size that is not a multiple of sizeof(T).
    template <typename T>
    std::vector<T> readBlob(const std::string_view &name) const {
      const Blob *found = nullptr;
      for (const auto &blob : blobs) {
        if (blob.name == name) {
          if (found) {
            throw std::runtime_error("Duplicate bundle blob: " + std::string(name));
          }
          found = &blob;
        }
      }

      if (!found || found->data.size() % sizeof(T) != 0) {
        throw std::runtime_error("Missing or invalid bundle blob: " + std::string(name));
      }

      std::vector<T> result(found->data.size() / sizeof(T));
      if (!result.empty()) {
        std::memcpy(result.data(), found->data.data(), found->data.size());
      }
      return result;
    }


    void setKey(const char *key, json value) { metadata[key] = std::move(value); }
    void write(const std::string_view &outPath);
    std::vector<u8> serialize(void);


   private:
    std::string name;
    // TODO: uuid of some kind?

    // Arbitrary metadata provided by the user as JSON.
    json metadata;

    std::vector<Blob> blobs;
  };

}  // namespace ren