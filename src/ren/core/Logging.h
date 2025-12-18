#pragma once
#include <fmt/core.h>
#include <functional>
#include <imgui/imgui.h>

namespace ren {

  enum class LogLevel { Debug, Info, Warning, Error, UserInterface };


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
  // grouped together, split by tab which can be closed. When the last tab is
  // closed, the window will close.  There is a shortcut to calling this
  // function. if you call logUI("Window>Tab", ...), it will forward to here.
  void logWindow(std::string windowGroup, std::string tab,
                 std::function<void(UiLogContext &)> uiFunc);

  template <typename T>
  void logInspection(std::string_view label, std::weak_ptr<T> item) {
    logUI(fmt::format("{} ##{}", label, (void*)item.lock().get()), [item](UiLogContext &ctx) {
      // Try to lock the weak pointer to get a shared pointer
      auto shared = item.lock();
      // If it wasn't possible, indicate that the item is gone
      // by marking the context as closed (this will free the memory used by this UI)
      if (!shared) {
        ctx.opened = false;
        return;
      }
      // We then require that T has an inspect() method
      if constexpr (requires { shared->inspect(); }) {
        // and call inspect on it.
        shared->inspect();
      } else {
        // If it doesn't, display that there's no inspect method
        ImGui::Text("No inspect() method for this type.");
      }
    });
  }


  template <typename T>
  void logInspection(std::string window, std::string_view label, std::shared_ptr<T> item) {
    logInspection<T>(fmt::format("{}>{}", window, label), std::weak_ptr<T>(item));
  }

  // Renders the log console in the current ImGui context
  void inspectLog();

}  // namespace ren