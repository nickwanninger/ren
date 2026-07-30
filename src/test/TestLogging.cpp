#include <ren/core/Logging.h>

namespace ren {

  // Reflection uses the engine logging facade for diagnostics. The headless
  // test target supplies a quiet sink instead of linking the renderer's ImGui
  // logging implementation.
  void logMessageln(LogLevel, std::string&&) {}

}  // namespace ren
