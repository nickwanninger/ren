#pragma once

#include <ren/types.h>

namespace ren {

  class BundleBuilder {
   public:
    BundleBuilder() = default;

    // No copy, no move
    BundleBuilder(const BundleBuilder &) = delete;
    BundleBuilder &operator=(const BundleBuilder &) = delete;
    BundleBuilder(BundleBuilder &&) = delete;
    BundleBuilder &operator=(BundleBuilder &&) = delete;




    u32 attachBlob(const std::span<u8> &data);
    template <typename T>
    u32 attachBlob(const std::vector<T> &data) {
      return attachBlob(std::span<u8>((u8*)data.data(), data.size() * sizeof(T)));
    }


    void setKey(const char *key, json value) { metadata[key] = std::move(value); }

    void write(const std::string_view &outPath);


   private:
    std::string name;
    // TODO: uuid of some kind?

    // Arbitrary metadata provided by the user as JSON.
    json metadata;

    struct Blob {
      std::vector<u8> data;
    };
    std::vector<Blob> blobs;
  };

}  // namespace ren