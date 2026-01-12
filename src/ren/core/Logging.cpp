#include <ren/core/Logging.h>
#include <deque>
#include <mutex>
#include <fmt/color.h>
#include <imgui/imgui.h>
#include <cstring>
#include <time.h>
#include <ren/types.h>

namespace ren {


  namespace {


    std::uint64_t getLogTimestamp() {
      using namespace std::chrono;
      return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    }

    class LogItem {
     public:
      LogItem(LogLevel level)
          : level(level)
          , timestamp_ms(getLogTimestamp()) {}
      virtual ~LogItem() = default;

      virtual void inspect(void) = 0;
      virtual bool searchMatches(const char* searchStr) const { return false; }


      LogLevel level;
      uint64_t timestamp_ms;
      bool pinned = false;  // always draw, even if scrollback is full.
    };


    class LogMessageItem : public LogItem {
     public:
      LogMessageItem(LogLevel level, std::string&& message)
          : LogItem(level)
          , message(std::move(message)) {}

      void inspect(void) override {
        // Render the log message in ImGui
        ImGui::TextUnformatted(message.c_str());
      }

     private:
      std::string message;
    };



    class LogUIItem;
    static std::unordered_map<std::string, LogUIItem*> g_uiLogContexts;
    class LogUIItem : public LogItem {
     public:
      LogUIItem(std::string_view label, std::function<void(UiLogContext& ctx)> uiFunc)
          : LogItem(LogLevel::UserInterface)
          , label(label)
          , uiFunc(std::move(uiFunc)) {
        this->pinned = true;
        g_uiLogContexts[this->label] = this;
      }

      virtual ~LogUIItem() { g_uiLogContexts.erase(label); }

      void inspect(void) override {
        ImGui::PushID(this);
        // Call the UI function to render additional UI


        if (ctx.opened) {
          ImGui::TextUnformatted(label.c_str());
          ImGui::SameLine();
          if (ImGui::SmallButton("X##LogUIItem")) {
            ctx.opened = false;
            uiFunc = nullptr;
          }

          if (uiFunc) uiFunc(ctx);
        }

        if (!ctx.opened) {
          ImGui::TextDisabled("%s (closed)", label.c_str());
          this->pinned = false;
        }

        // ImGui::TreePop();
        // }
        ImGui::PopID();
      }
      UiLogContext ctx;
      std::string label;
      std::function<void(UiLogContext&)> uiFunc;
    };


    static std::deque<Box<LogItem>> g_logMessages;
    static size_t g_logMessageBytes = 0;
    static std::mutex g_logMutex;


  };  // namespace

  void logMessageln(LogLevel level, std::string&& message) {
    std::lock_guard<std::mutex> lock(g_logMutex);

    switch (level) {
      case LogLevel::Debug:
        fmt::print(fmt::emphasis::bold | fg(fmt::terminal_color::cyan), "[ DEBG ] ");
        break;
      case LogLevel::Info:
        // fmt::print(fmt::emphasis::bold | fg(fmt::terminal_color::green), "[ INFO ] ");
        break;
      case LogLevel::Warning:
        fmt::print(fmt::emphasis::bold | fg(fmt::terminal_color::yellow), "[ WARN ] ");
        break;
      case LogLevel::Error:
        fmt::print(fmt::emphasis::bold | fg(fmt::terminal_color::red), "[ ERR! ] ");
        break;
      default: break;
    }

    fmt::print("{}\n", message);

    g_logMessageBytes += message.size() + sizeof(LogItem);
    g_logMessages.push_back(ren::makeBox<LogMessageItem>(level, std::move(message)));
  }

  // Simple trim helper
  static std::string_view trim(std::string_view s) {
    auto start = s.find_first_not_of(" \t\n\r");
    if (start == std::string_view::npos) return "";

    auto end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
  }



  static std::pair<std::string, std::string> split_and_strip(std::string_view input) {
    auto pos = input.find('>');
    if (pos == std::string_view::npos) {
      // No '>' found - return whole string as first element
      auto trimmed = trim(input);
      return {std::string(trimmed), ""};
    }

    auto first = trim(input.substr(0, pos));
    auto second = trim(input.substr(pos + 1));

    return {std::string(first), std::string(second)};
  }


  inline void logUIRaw(std::string_view label, std::function<void(UiLogContext& ctx)> uiFunc) {
    fmt::println("[Log UI] Registering UI log: {}", label);
    for (auto& pair : g_uiLogContexts) {
      fmt::println("  Existing UI log: {}", pair.first);
    }
    std::lock_guard<std::mutex> lock(g_logMutex);

    // If the label already exists, we need to replace it's UI function
    auto it = g_uiLogContexts.find(std::string(label));
    if (it != g_uiLogContexts.end()) {
      fmt::println("[Log UI] Updating existing UI log: {}", label);
      it->second->uiFunc = std::move(uiFunc);
      it->second->ctx.opened = true;
      it->second->pinned = true;  // always drawn
      return;
    }

    g_logMessages.push_back(ren::makeBox<LogUIItem>(label, std::move(uiFunc)));
  }


  void logUI(std::string_view label, std::function<void(UiLogContext& ctx)> uiFunc) {
    // If the label contains '>', split into window and tab
    auto [windowGroup, tab] = split_and_strip(label);
    if (!tab.empty()) {
      logWindow(windowGroup, tab, std::move(uiFunc));
      return;
    }


    logUIRaw(label, std::move(uiFunc));
  }

  void logWindow(std::string windowGroup, std::string tab,
                 std::function<void(UiLogContext& ctx)> uiFunc) {
    logUIRaw(fmt::format("{} > {}", windowGroup, tab), [=](UiLogContext& ctx) {
      ImGui::SameLine();
      ImGui::TextDisabled("Opened in Window");
      ImGui::Begin(windowGroup.c_str(), &ctx.opened);
      ImGui::BeginTabBar(windowGroup.c_str());
      if (ImGui::BeginTabItem(tab.data(), &ctx.opened)) {
        uiFunc(ctx);
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
      ImGui::End();
    });
  }

  void inspectLog() {
    REN_PROFILE_FUNCTION();
    ImGui::SetNextWindowSizeConstraints(ImVec2(350, 200), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::Begin("Log");
    // Static state for the log inspector
    static bool showDebug = true;
    static bool showInfo = true;
    static bool showWarning = true;
    static bool showError = true;
    static char searchBuffer[256] = "";
    static bool autoScroll = true;
    static bool scrollToBottom = false;
    static int maxScrollbackDisplay = 10000;


    auto nowTimestamp = getLogTimestamp();

    // Search box
    ImGui::SetNextItemWidth(-100.0f);
    if (ImGui::InputText("##search", searchBuffer, IM_ARRAYSIZE(searchBuffer))) {
      // Search text changed, could trigger filtering
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Search")) { searchBuffer[0] = '\0'; }



    // Filter controls
    ImGui::BeginGroup();
    ImGui::Text("Filters:");
    ImGui::SameLine();
    ImGui::Checkbox("Debug", &showDebug);
    ImGui::SameLine();
    ImGui::Checkbox("Info", &showInfo);
    ImGui::SameLine();
    ImGui::Checkbox("Warning", &showWarning);
    ImGui::SameLine();
    ImGui::Checkbox("Error", &showError);
    ImGui::EndGroup();
    ImGui::SameLine();


    // Controls
    ImGui::Checkbox("Auto-scroll", &autoScroll);
    ImGui::SameLine();
    if (ImGui::Button("Clear Log")) {
      // Only clear non-pinned messages

      std::lock_guard<std::mutex> lock(g_logMutex);

      g_logMessages.erase(std::remove_if(g_logMessages.begin(), g_logMessages.end(),
                                         [](const Box<LogItem>& item) { return !item->pinned; }),
                          g_logMessages.end());
      g_logMessageBytes = 0;
    }
    ImGui::SameLine();
    ImGui::Text("Total Bytes: %fMB", g_logMessageBytes / (1024.0f * 1024.0f));

    ImGui::Separator();

    ImGui::BeginChild("LogScrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    // Use smaller font for log messages
    // ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
    ImGui::SetWindowFontScale(0.9f);

    // Lock the mutex while we iterate
    // std::lock_guard<std::mutex> lock(g_logMutex);

    // Filter search string (case-insensitive)
    const bool hasSearch = searchBuffer[0] != '\0';

    size_t startIdx = g_logMessages.size() > maxScrollbackDisplay
                          ? g_logMessages.size() - maxScrollbackDisplay
                          : 0;

    for (size_t i = 0; i < g_logMessages.size(); ++i) {
      const auto& logMsg = g_logMessages[i];
      if (i < startIdx && !logMsg->pinned) { continue; }

      // Filter by log level
      bool shouldShow = false;
      switch (logMsg->level) {
        case LogLevel::Debug: shouldShow = showDebug; break;
        case LogLevel::Info: shouldShow = showInfo; break;
        case LogLevel::Warning: shouldShow = showWarning; break;
        case LogLevel::Error: shouldShow = showError; break;
        default: shouldShow = true; break;
      }


      if (!shouldShow) continue;

      // Filter by search text (case-insensitive)
      if (hasSearch) {
        if (!logMsg->searchMatches(searchBuffer)) { continue; }
      }

      // Determine color based on log level
      ImVec4 color;
      const char* prefix;
      switch (logMsg->level) {
        case LogLevel::Debug:
          color = ImVec4(0.0f, 0.8f, 0.8f, 1.0f);  // Cyan
          prefix = "D";
          break;
        case LogLevel::Info:
          color = ImVec4(0.0f, 0.8f, 0.0f, 1.0f);  // Green
          prefix = NULL;
          break;
        case LogLevel::Warning:
          color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);  // Yellow
          prefix = "W";
          break;
        case LogLevel::Error:
          color = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);  // Red
          prefix = "!";
          break;
        case LogLevel::UserInterface:
          color = ImVec4(0.0f, 0.8f, 0.0f, 1.0f);  // Green
          prefix = "UI";
          break;
      }

      // Render the log message
      char timebuf[64];
      if (logMsg->pinned) {
        std::strcpy(timebuf, "[PINNED]");
      } else {
        time_t seconds = (time_t)(logMsg->timestamp_ms / 1000);

        strftime(timebuf, sizeof(timebuf), "%H:%M:%S", localtime(&seconds));
      }

      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
      ImGui::TextUnformatted(timebuf);
      ImGui::SameLine();
      ImGui::PopStyleColor();

      if (prefix) {
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(prefix);
        ImGui::SameLine();
        ImGui::PopStyleColor();
      }


      logMsg->inspect();
    }

    // Auto-scroll to bottom
    if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) { scrollToBottom = true; }

    if (scrollToBottom) {
      ImGui::SetScrollHereY(1.0f);
      scrollToBottom = false;
    }

    // Track when new messages arrive for auto-scroll
    static size_t lastMessageCount = 0;
    if (g_logMessages.size() > lastMessageCount) {
      if (autoScroll) { scrollToBottom = true; }
      lastMessageCount = g_logMessages.size();
    }

    ImGui::EndChild();
    ImGui::End();  // Log window
  }

}  // namespace ren
