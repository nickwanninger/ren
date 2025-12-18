#include "./DeprecationLogger.h"
#include <unordered_set>
#include <ren/types.h>
namespace ren {


  void printDeprecationWarning(const char* function, const char* file, int line,
                               std::span<void*> backtrace) {
    ren::warnln("=== Deprecation Warning ===");
    ren::warnln("Function: '{}'", function);
    ren::warnln("Location: {}:{}", file, line);
    ren::warnln("Backtrace:");

    char** symbols = backtrace_symbols(backtrace.data(), static_cast<int>(backtrace.size()));
    for (size_t i = 0; i < backtrace.size(); ++i) {
      ren::warnln("  {}", symbols[i]);
    }
    free(symbols);
    ren::warnln("===========================");
  }

}  // namespace ren