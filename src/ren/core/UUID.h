#pragma once

#include <ren/types.h>
#include <unordered_map>  // for std::hash

namespace ren {


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
