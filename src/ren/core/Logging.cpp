#include <ren/core/Logging.h>
#include <deque>
#include <mutex>
#include <fmt/color.h>
#include <imgui/imgui.h>
#include <cstring>

namespace ren {


  namespace {


    struct LogMessage {
      LogLevel level;
      // Owned std::string of the message.
      std::string message;
    };


    static std::deque<LogMessage> g_logMessages;
    static size_t g_logMessageBytes = 0;
    static std::mutex g_logMutex;



  };  // namespace

  void logMessageln(LogLevel level, std::string &&message) {
    std::lock_guard<std::mutex> lock(g_logMutex);

    switch (level) {
      case LogLevel::Debug:
        fmt::print(fmt::emphasis::bold | fg(fmt::terminal_color::cyan), "[ DEBG ] ");
        break;
      case LogLevel::Info:
        fmt::print(fmt::emphasis::bold | fg(fmt::terminal_color::green), "[ INFO ] ");
        break;
      case LogLevel::Warning:
        fmt::print(fmt::emphasis::bold | fg(fmt::terminal_color::yellow), "[ WARN ] ");
        break;
      case LogLevel::Error:
        fmt::print(fmt::emphasis::bold | fg(fmt::terminal_color::red), "[ ERR! ] ");
        break;
    }

    fmt::print("{}\n", message);

    g_logMessageBytes += message.size() + sizeof(LogMessage);
    g_logMessages.push_back({level, std::move(message)});
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



    // Search box
    ImGui::SetNextItemWidth(-100.0f);
    if (ImGui::InputText("##search", searchBuffer, IM_ARRAYSIZE(searchBuffer))) {
      // Search text changed, could trigger filtering
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Search")) {
      searchBuffer[0] = '\0';
    }

    
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
    ImGui::Text("Total Bytes: %zu", g_logMessageBytes);

    ImGui::Separator();

    // Main scrolling region
    ImGui::BeginChild("LogScrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    // Use smaller font for log messages
    // ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
    ImGui::SetWindowFontScale(0.85f);

    // Lock the mutex while we iterate
    std::lock_guard<std::mutex> lock(g_logMutex);

    // Filter search string (case-insensitive)
    const bool hasSearch = searchBuffer[0] != '\0';

    for (const auto& logMsg : g_logMessages) {
      // Filter by log level
      bool shouldShow = false;
      switch (logMsg.level) {
        case LogLevel::Debug:   shouldShow = showDebug; break;
        case LogLevel::Info:    shouldShow = showInfo; break;
        case LogLevel::Warning: shouldShow = showWarning; break;
        case LogLevel::Error:   shouldShow = showError; break;
      }

      if (!shouldShow) continue;

      // Filter by search text (case-insensitive)
      if (hasSearch) {
        // Simple case-insensitive search
        bool found = false;
        const char* msgPtr = logMsg.message.c_str();
        const size_t searchLen = strlen(searchBuffer);

        for (size_t i = 0; i <= logMsg.message.length() - searchLen; ++i) {
          bool match = true;
          for (size_t j = 0; j < searchLen; ++j) {
            if (tolower(msgPtr[i + j]) != tolower(searchBuffer[j])) {
              match = false;
              break;
            }
          }
          if (match) {
            found = true;
            break;
          }
        }

        if (!found) continue;
      }

      // Determine color based on log level
      ImVec4 color;
      const char* prefix;
      switch (logMsg.level) {
        case LogLevel::Debug:
          color = ImVec4(0.0f, 0.8f, 0.8f, 1.0f);  // Cyan
          prefix = "[DEBUG] ";
          break;
        case LogLevel::Info:
          color = ImVec4(0.0f, 0.8f, 0.0f, 1.0f);  // Green
          prefix = "[INFO]  ";
          break;
        case LogLevel::Warning:
          color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);  // Yellow
          prefix = "[WARN]  ";
          break;
        case LogLevel::Error:
          color = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);  // Red
          prefix = "[ERROR] ";
          break;
      }

      // Render the log message
      ImGui::PushStyleColor(ImGuiCol_Text, color);
      ImGui::TextUnformatted(prefix);
      ImGui::SameLine();
      ImGui::PopStyleColor();
      ImGui::TextUnformatted(logMsg.message.c_str());
    }

    // Auto-scroll to bottom
    if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
      scrollToBottom = true;
    }

    if (scrollToBottom) {
      ImGui::SetScrollHereY(1.0f);
      scrollToBottom = false;
    }

    // Track when new messages arrive for auto-scroll
    static size_t lastMessageCount = 0;
    if (g_logMessages.size() > lastMessageCount) {
      if (autoScroll) {
        scrollToBottom = true;
      }
      lastMessageCount = g_logMessages.size();
    }

    ImGui::EndChild();
  }

}  // namespace ren