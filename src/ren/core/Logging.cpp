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



    class LogUIItem : public LogItem {
     public:
      LogUIItem(std::string_view label, std::function<void(UiLogContext& ctx)> uiFunc)
          : LogItem(LogLevel::Info)
          , label(label)
          , uiFunc(std::move(uiFunc)) {}

      void inspect(void) override {
        ImGui::PushID(this);
        // Call the UI function to render additional UI


        if (ctx.opened) {
          ImGui::TextUnformatted(label.c_str());
          ImGui::SameLine();
          if (ImGui::SmallButton("Close")) {
            ctx.opened = false;
            uiFunc = nullptr;
          }

          if (uiFunc) uiFunc(ctx);
        }

        if (!ctx.opened) { ImGui::TextDisabled("%s (closed)", label.c_str()); }

        // ImGui::TreePop();
        // }
        ImGui::PopID();
      }

     private:
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
    }

    fmt::print("{}\n", message);

    g_logMessageBytes += message.size() + sizeof(LogItem);
    g_logMessages.push_back(ren::makeBox<LogMessageItem>(level, std::move(message)));
  }


  void logUI(std::string_view label, std::function<void(UiLogContext& ctx)> uiFunc) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_logMessages.push_back(ren::makeBox<LogUIItem>(label, std::move(uiFunc)));
  }

  void logWindow(std::string windowGroup, std::string tab,
                 std::function<void(UiLogContext& ctx)> uiFunc) {
    logUI(fmt::format("{} > {}", windowGroup, tab), [=](UiLogContext& ctx) {
      ImGui::Begin(windowGroup.c_str());
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
      std::lock_guard<std::mutex> lock(g_logMutex);
      g_logMessages.clear();
      g_logMessageBytes = 0;
    }
    ImGui::SameLine();
    ImGui::Text("Total Bytes: %fMB", g_logMessageBytes / (1024.0f * 1024.0f));

    ImGui::Separator();

    // Main scrolling region
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

    for (size_t i = startIdx; i < g_logMessages.size(); ++i) {
      const auto& logMsg = g_logMessages[i];

      // Filter by log level
      bool shouldShow = false;
      switch (logMsg->level) {
        case LogLevel::Debug: shouldShow = showDebug; break;
        case LogLevel::Info: shouldShow = showInfo; break;
        case LogLevel::Warning: shouldShow = showWarning; break;
        case LogLevel::Error: shouldShow = showError; break;
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
      }

      // Render the log message

      char timebuf[64];
      time_t seconds = (time_t)(logMsg->timestamp_ms / 1000);
      strftime(timebuf, sizeof(timebuf), "%H:%M:%S", localtime(&seconds));

      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
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
  }

}  // namespace ren