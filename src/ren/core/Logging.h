#pragma once
#include <fmt/core.h>
#include <functional>

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

  struct UiLogContext {
    bool opened = true;
  };

  void logUI(std::string_view label, std::function<void(UiLogContext &)> uiFunc);
  inline void logUI(std::string_view label, std::function<void()> uiFunc) {
    logUI(label, [uiFunc = std::move(uiFunc)](UiLogContext &) { uiFunc(); });
  }


  // Log a tab in a window grouping. All logs into the same windowGroup will be
  // grouped together, split by tab which can be closed. When the last tab is closed,
  // the window will close.
  void logWindow(std::string windowGroup, std::string tab,
                 std::function<void(UiLogContext &)> uiFunc);

  // Renders the log console in the current ImGui context
  void inspectLog();

}  // namespace ren