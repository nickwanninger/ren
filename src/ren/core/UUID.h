#pragma once

#include <ren/types.h>
#include <unordered_map>  // for std::hash

namespace ren {


  class UUID {
   public:
    UUID();
    UUID(u64 uuid);
    UUID(const UUID&) = default;

    operator u64() const { return m_UUID; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(UUID, m_UUID);

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
