#include <ren/assets/AssetStore.h>
#include <hash-library/sha256.h>

namespace ren {


  // Encodes `bytes` with the base32 encoding that Nix uses for store paths.
  // The alphabet omits 'e', 'o', 'u' and 't'. The output is the same as
  // `nix-hash --to-base32`: Nix reads the bytes as one little-endian number
  // and writes its 5-bit digits from the most significant digit first. This
  // is not RFC 4648 base32. A 20-byte digest gives 32 characters.
  static std::string toNixBase32(const u8 *bytes, size_t size) {
    static constexpr char alphabet[] = "0123456789abcdfghijklmnpqrsvwxyz";

    if (size == 0) {
      return {};
    }

    size_t length = (size * 8 - 1) / 5 + 1;
    std::string out;
    out.reserve(length);
    for (size_t n = length; n-- > 0;) {
      size_t bit = n * 5;
      size_t i = bit / 8;
      size_t j = bit % 8;
      unsigned c = bytes[i] >> j;
      if (i + 1 < size) {
        c |= unsigned(bytes[i + 1]) << (8 - j);
      }
      out.push_back(alphabet[c & 0x1f]);
    }
    return out;
  }



  // SHA-256 of `data`, XOR-folded to 160 bits (the same as Nix's
  // compressHash()), in Nix base32. This keeps store paths at 32 characters.
  static std::string foldedSha256(const void *data, size_t size) {
    SHA256 hasher;
    hasher.add(data, size);
    u8 digest[SHA256::HashBytes];
    hasher.getHash(digest);

    u8 folded[20] = {};
    for (size_t i = 0; i < sizeof(digest); i++) {
      folded[i % sizeof(folded)] ^= digest[i];
    }

    return toNixBase32(folded, sizeof(folded));
  }

  static std::string legalizeName(std::string_view name) {
    std::string result;
    result.reserve(name.size());
    for (char c : name) {
      if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
        result.push_back('_');
      } else {
        result.push_back(c);
      }
    }
    return result;
  }

  std::string AssetStore::hash(ByteSpan data, std::string_view metaJson, std::string_view namev) {
    REN_PROFILE_SCOPE("Hash");
    std::string name = legalizeName(namev);

    SHA256 inner;
    inner.add(data.data, data.size);
    inner.add(metaJson.data(), metaJson.size());
    std::string fingerprint = fmt::format("blob:sha256:{}:{}", inner.getHash(), name);
    return fmt::format("{}-{}", foldedSha256(fingerprint.data(), fingerprint.size()), name);
  }


  AssetStore::VerifyResult AssetStore::verify(std::string_view key) {
    // A key is "<32 hash characters>-<name>".
    if (key.size() < 33 || key[32] != '-') {
      return VerifyResult::BadKey;
    }
    std::string_view name = key.substr(33);

    std::vector<u8> data;
    std::vector<u8> meta;
    if (!source.load(fmt::format("{}/{}/data.blob", ASSET_STORE_DIR, key), data) ||
        !source.load(fmt::format("{}/{}/meta.json", ASSET_STORE_DIR, key), meta)) {
      return VerifyResult::Missing;
    }

    std::string_view metaJson(reinterpret_cast<const char *>(meta.data()), meta.size());
    return hash(ByteSpan(data), metaJson, name) == key ? VerifyResult::Ok : VerifyResult::Mismatch;
  }


  bool AssetStore::listKeys(std::function<void(std::string_view)> callback) {
    return source.list(ASSET_STORE_DIR, [&](const AssetDirEntry &entry) {
      if (entry.type == AssetEntryType::Directory) {
        callback(entry.name);
      }
    });
  }


  ren::Option<std::string> AssetStore::storeBlob(const ByteSpan data, const json &meta, std::string_view namev) {
    REN_PROFILE_SCOPE("AssetStore::storeBlob");
    std::string metastr = meta.dump(2);  // pretty print with 2 spaces
    std::string hash = AssetStore::hash(data, metastr, namev);

    auto dir = fmt::format("{}/{}", ASSET_STORE_DIR, hash);
    if (!source.mkdir(dir)) {
      ren::println("[Asset Store] Failed to create directory for asset '{}'", dir);
      return ren::None;
    }


    ren::println("[Asset Store] Storing asset '{}/data.blob'", dir);
    source.store(fmt::format("{}/data.blob", dir), data);
    source.store(fmt::format("{}/meta.json", dir), metastr.c_str());

    return hash;
  }

}  // namespace ren
