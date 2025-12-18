#pragma once
#include <fmt/core.h>

namespace ren {

  enum class LogLevel { Debug, Info, Warning, Error };


  // logs in Ren are always single line messages. It is expected that `message` has no newline.
  void logMessageln(LogLevel level, std::string &&message);

  template <typename... T>
  inline void println(fmt::format_string<T...> fmt, T &&...args) {
    logMessageln(LogLevel::Info, fmt::format(fmt, static_cast<T &&>(args)...));
  }

  template <typename... T>
  inline void dbgln(fmt::format_string<T...> fmt, T &&...args) {
    logMessageln(LogLevel::Debug, fmt::format(fmt, static_cast<T &&>(args)...));
  }

  template <typename... T>
  inline void warnln(fmt::format_string<T...> fmt, T &&...args) {
    logMessageln(LogLevel::Warning, fmt::format(fmt, static_cast<T &&>(args)...));
  }


  template <typename... T>
  inline void errln(fmt::format_string<T...> fmt, T &&...args) {
    logMessageln(LogLevel::Error, fmt::format(fmt, static_cast<T &&>(args)...));
  }

  // Renders the log console in the current ImGui context
  void inspectLog();

}  // namespace ren