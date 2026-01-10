#pragma once

#include <ren/types.h>
#include <random>
#include <fmt/format.h>
#include <ren/core/Option.h>

namespace ren {


  class UUIDv7 {
   public:
    UUIDv7() { generate(); }

    std::string to_string() const {
      return fmt::format("{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}", bytes[0], bytes[1],
                         bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13],
                         bytes[14], bytes[15]);
    }

    const std::array<uint8_t, 16>& raw() const { return bytes; }

    static Option<UUIDv7> parse(std::string_view str) {
      UUIDv7 uuid;

      // Expected format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
      if (str.length() != 36) {
        return None;
      }

      // Check hyphens are in the right places
      if (str[8] != '-' || str[13] != '-' || str[18] != '-' || str[23] != '-') {
        return None;
      }

      // Parse hex digits
      size_t byte_idx = 0;
      for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '-') {
          continue;
        }

        char c1 = str[i];
        char c2 = str[++i];

        auto hex_to_nibble = [](char c) -> std::optional<uint8_t> {
          if (c >= '0' && c <= '9') {
            return c - '0';
          }
          if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
          }
          if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
          }
          return std::nullopt;
        };

        auto n1 = hex_to_nibble(c1);
        auto n2 = hex_to_nibble(c2);

        if (!n1 || !n2) {
          return None;
        }

        uuid.bytes[byte_idx++] = (*n1 << 4) | *n2;
      }

      return Some(uuid);
    }


   private:
    friend struct fmt::formatter<UUIDv7>;

    void generate() {
      // Get Unix timestamp in milliseconds
      auto now = std::chrono::system_clock::now();
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

      // Fill timestamp (48 bits)
      bytes[0] = (ms >> 40) & 0xFF;
      bytes[1] = (ms >> 32) & 0xFF;
      bytes[2] = (ms >> 24) & 0xFF;
      bytes[3] = (ms >> 16) & 0xFF;
      bytes[4] = (ms >> 8) & 0xFF;
      bytes[5] = ms & 0xFF;

      // Random bytes for the rest
      thread_local std::random_device rd;
      thread_local std::mt19937_64 gen(rd());
      std::uniform_int_distribution<uint8_t> dist(0, 255);

      for (int i = 6; i < 16; ++i) {
        bytes[i] = dist(gen);
      }

      // Set version (4 bits) to 7
      bytes[6] = (bytes[6] & 0x0F) | 0x70;

      // Set variant (2 bits) to RFC 4122
      bytes[8] = (bytes[8] & 0x3F) | 0x80;
    }

    std::array<uint8_t, 16> bytes;
  };



  // using UUID = UUIDv7;


  class UUID {
   public:
    static constexpr u64 null = 0x0;
    UUID();
    UUID(u64 uuid);
    UUID(const UUID&) = default;

    operator u64() const { return m_UUID; }

    friend void to_json(json& j, const UUID& uuid) {
      if (uuid.m_UUID == UUID::null) {
        j = "null";
      } else {
        j = uuid.m_UUID;
      }
    }

    friend void from_json(const json& j, UUID& uuid) {
      if (j == "null") {
        uuid.m_UUID = UUID::null;
      } else {
        uuid.m_UUID = j.get<u64>();
      }
    }

   private:
    u64 m_UUID;
  };


  // Inherit from this class to get a UUID for a given class.
  class HasUUID {
   public:
    auto getUUID() const { return m_uuid; }

   private:
    UUID m_uuid;
  };


}  // namespace ren


namespace std {
  template <>
  struct hash<ren::UUID> {
    std::size_t operator()(const ren::UUID& uuid) const { return (u64)uuid; }
  };
}  // namespace std




namespace nlohmann {
  template <>
  struct adl_serializer<ren::UUIDv7> {
    static void to_json(json& j, const ren::UUIDv7& v) { j = v.to_string(); }

    static void from_json(const json& j, ren::UUIDv7& v) {
      std::string uuid_str;
      j.get_to(uuid_str);
      auto uuid_opt = ren::UUIDv7::parse(uuid_str);
      if (uuid_opt) {
        v = uuid_opt.unwrap();
      } else {
        throw std::runtime_error("Invalid UUIDv7 string: " + uuid_str);
      }
    }
  };
}  // namespace nlohmann


template <>
struct fmt::formatter<ren::UUIDv7> {
  constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

  template <typename FormatContext>
  auto format(const ren::UUIDv7& uuid, FormatContext& ctx) const {
    const auto& b = uuid.bytes;
    return fmt::format_to(ctx.out(), "{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}", b[0],
                          b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
  }
};