#pragma once

#include <chrono>

namespace ren {

  using Time = decltype(std::chrono::high_resolution_clock::now());

  auto timestamp() { return std::chrono::high_resolution_clock::now(); }

  uint64_t elapsed_ns(Time start, Time end) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
  }

}  // namespace ren