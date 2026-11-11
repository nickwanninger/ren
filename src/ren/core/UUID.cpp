#include <ren/core/UUID.h>
#include <mutex>
#include <random>

namespace ren {

  static std::random_device s_RandomDevice;
  static std::mt19937_64 s_Engine(s_RandomDevice());
  static std::uniform_int_distribution<uint64_t> s_UniformDistribution;

  UUID::UUID()
      : m_UUID(UUID::null) {}

  UUID::UUID(uint64_t uuid)
      : m_UUID(uuid) {
  }


  UUID UUID::generate() {
    // Import workers generate IDs in parallel, and the engine is not thread-safe.
    static std::mutex mutex;
    std::scoped_lock lock(mutex);
    return UUID(s_UniformDistribution(s_Engine));
  }


  Option<UUID> UUID::parse(std::string_view str) {
    if (str == "null") {
      return Some(UUID(UUID::null));
    }
    if (str.length() != 16) {
      return None;
    }
    u64 value = 0;
    for (char c : str) {
      value <<= 4;
      if (c >= '0' && c <= '9') {
        value |= (c - '0');
      } else if (c >= 'a' && c <= 'f') {
        value |= (c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        value |= (c - 'A' + 10);
      } else {
        return None;
      }
    }
    return Some(UUID(value));
  }

}  // namespace ren