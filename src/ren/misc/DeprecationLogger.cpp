#include "./DeprecationLogger.h"


namespace ren {


  void printDeprecationWarning(const char* function, const char* file, int line,
                               std::span<void*> backtrace) {
    fmt::print("\e[31m");
    fmt::println("=== Deprecation Warning ===");
    fmt::println("Function: '{}'", function);
    fmt::println("Location: {}:{}", file, line);
    fmt::println("Backtrace:");
    char** symbols = backtrace_symbols(backtrace.data(), static_cast<int>(backtrace.size()));
    for (size_t i = 0; i < backtrace.size(); ++i) {
      fmt::println("  {}", symbols[i]);
    }
    free(symbols);
    fmt::println("===========================");
    fmt::print("\e[0m");
  }

}  // namespace ren