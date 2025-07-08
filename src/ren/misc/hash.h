#pragma once

#include <ren/types.h>

namespace ren {
  void hash(u64 &state, const void *data, size_t size);

  template <typename T>
  inline void hash(u64 &state, const T &value) {
    hash(state, &value, sizeof(T));
  }


  inline void hashCombine(u64 &state, u64 other) {
    state ^= other + 0x9e3779b9 + (state << 6) + (state >> 2);  // idk...
  }

  template <typename T>
  inline void hashStd(u64 &state, const T &value) {
    hashCombine(state, std::hash<T>()(value));
  }


  inline u64 hash(const void *data, size_t size) {
    u64 state = 0;
    hash(state, data, size);
    return state;
  }

  template <typename T>
  inline u64 hash(const T &value) {
    u64 state = 0;
    hash_impl(state, value, typename std::is_member_function_pointer<decltype(&T::hash)>::type());
    return state;
  }

  // Implementation for types that have a hash() method
  template <typename T>
  inline void hash_impl(u64 &state, const T &value, std::true_type) {
    hashCombine(state, value.hash());
  }

  // Fallback implementation for types without hash() method
  template <typename T>
  inline void hash_impl(u64 &state, const T &value, std::false_type) {
    hash(state, value);
  }
}  // namespace ren