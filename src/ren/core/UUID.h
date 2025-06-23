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

   private:
    u64 m_UUID;
  };

}  // namespace ren


namespace std {
  template <>
  struct hash<ren::UUID> {
    std::size_t operator()(const ren::UUID& uuid) const { return (u64)uuid; }
  };
}  // namespace std
