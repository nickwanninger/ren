#include "./DeprecationLogger.h"
#include <unordered_set>

namespace ren {


  void printDeprecationWarning(const char* function, const char* file, int line,
                               std::span<void*> backtrace) {
    static std::unordered_set<u64> seenBacktraceHashes;


    u64 hash;
    for (auto ptr : backtrace) {
      hash ^= reinterpret_cast<u64>(ptr) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    }

    if (seenBacktraceHashes.find(hash) != seenBacktraceHashes.end()) { return; }
    seenBacktraceHashes.insert(hash);

    fmt::println("\e[31m=== Deprecation Warning ===\e[0m");
    fmt::println("Function: '{}'", function);
    fmt::println("Location: {}:{}", file, line);
    fmt::println("Backtrace:");

    char** symbols = backtrace_symbols(backtrace.data(), static_cast<int>(backtrace.size()));
    for (size_t i = 0; i < backtrace.size(); ++i) {
      fmt::println("  {}", symbols[i]);
    }
    free(symbols);
    fmt::println("===========================");
  }

}  // namespace ren