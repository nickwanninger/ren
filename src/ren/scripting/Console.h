#pragma once

#include <string.h>
#include <ren/scripting/Scheme.h>
#include <ren/types.h>
#include <imgui/imgui.h>

namespace ren {

  struct SchemeConsole {
    ren::Scheme& S;

    // Scrollback and input
    ImVector<char*> Items;
    char InputBuf[4096];
    ImVector<char*> History;
    int HistoryPos = -1;
    bool AutoScroll = true;
    bool ScrollToBottom = false;
    bool Open = true;

    // Prompt string
    std::string Prompt = "> ";

    SchemeConsole(ren::Scheme& scheme)
        : S(scheme) {
      ClearLog();
      memset(InputBuf, 0, sizeof(InputBuf));
      InitS7();
    }
    ~SchemeConsole() {
      ClearLog();
      for (int i = 0; i < History.Size; ++i)
        free(History[i]);
    }

    void ClearLog() {
      for (int i = 0; i < Items.Size; ++i)
        free(Items[i]);
      Items.clear();
      Items.shrink(0);
    }

    void AddLog(const char* fmt, ...) IM_FMTARGS(2) {
      va_list args;
      va_start(args, fmt);
      char buf[8192];
      vsnprintf(buf, sizeof(buf), fmt, args);
      va_end(args);
      Items.push_back(strdup(buf));
      if (AutoScroll) ScrollToBottom = true;
    }

    // Initialize s7 and define a safe REPL helper that:
    // - reads multiple forms from a string
    // - evals each
    // - prints non-unspecified results (write + newline)
    // - captures ALL printed output via with-output-to-string
    // - catches any exception and prints it as "error: ..."
    void InitS7() {
      AddLog("[s7] initialized. Type Scheme forms; multiple expressions allowed per line.");
    }

    // Evaluate one line of Scheme and return the captured output.
    // We call (safe-repl <line-as-string>) and stringify the result.
    std::string EvalLine(const char* code) {
      auto sc = S.context();
      /* Create ports: input from string, output to string. */
      s7_pointer in_port = s7_open_input_string(sc, code);
      s7_pointer out_port = s7_open_output_string(sc);

      /* Save/redirect current-output-port. */
      s7_pointer old_out = s7_current_output_port(sc);
      s7_set_current_output_port(sc, out_port);

      /* Read/eval loop. */
      for (;;) {
        s7_pointer expr = s7_read(sc, in_port);
        if (expr == s7_eof_object(sc)) break;

        /* Evaluate in persistent top-level env so (define …) sticks globally. */
        s7_pointer val = s7_eval(sc, expr, s7_rootlet(sc));

        /* Print values (like a REPL) unless they're (unspecified). */
        if (!s7_is_unspecified(sc, val)) {
          // attempt to call (pretty-print val) in scheme, if available
          s7_pointer pp_sym = s7_make_symbol(sc, "pretty-print");
          s7_pointer pp_fn = s7_eval(sc, pp_sym, s7_rootlet(sc));
          if (s7_is_procedure(pp_fn)) {
            // quote the value to avoid it being evaluated again.
            s7_call(sc, pp_fn, s7_list(sc, 1, val));
          } else {
            s7_display(sc, val, s7_current_output_port(sc));
          }
          s7_newline(sc, s7_current_output_port(sc));
        }
      }

      /* Restore output and fetch captured text. */
      s7_set_current_output_port(sc, old_out);

      const char* captured = s7_get_output_string(sc, out_port);
      char* result = NULL;
      if (captured) {
        result = (char*)malloc(strlen(captured) + 1);
        if (result) strcpy(result, captured);
      } else {
        result = (char*)malloc(1);
        if (result) result[0] = '\0';
      }

      /* Close the ports (optional; s7 GC would reclaim eventually). */
      s7_close_input_port(sc, in_port);
      s7_close_output_port(sc, out_port);

      return result;
      // // Build: (safe-repl ".....")
      // // Minimal escaping for backslash and quotes in a C-string literal to Scheme string
      // std::string code = "(safe-repl \"";
      // for (const char* p = line; *p; ++p) {
      //   char c = *p;
      //   if (c == '\\' || c == '\"') {
      //     code.push_back('\\');
      //     code.push_back(c);
      //   } else if (c == '\n') {
      //     code += "\\n";
      //   } else if (c == '\r') { /* skip */
      //   } else {
      //     code.push_back(c);
      //   }
      // }
      // code += "\")";
      // ren::println("Eval code: {}", code);

      // s7_pointer out = S.eval(code.c_str());
      // if (s7_is_string(out)) {
      //   return std::string(s7_string(out));
      // } else {
      //   // Unexpected; stringify anyway
      //   return s7_object_to_c_string(S.context(), out);
      // }

      return "";
    }

    void ExecCommand(const char* cmd) {
      // Echo the prompt + command
      AddLog("%s%s", Prompt.c_str(), cmd);

      // Append to history (dedup last)
      if (History.Size == 0 || strcmp(History.back(), cmd) != 0) History.push_back(strdup(cmd));
      HistoryPos = -1;

      // Evaluate in s7
      std::string result = EvalLine(cmd);
      if (!result.empty()) {
        // Split result into lines to keep log behavior familiar
        size_t start = 0;
        while (start < result.size()) {
          size_t nl = result.find('\n', start);
          std::string line =
              (nl == std::string::npos) ? result.substr(start) : result.substr(start, nl - start);
          if (!line.empty()) AddLog("%s", line.c_str());
          if (nl == std::string::npos) break;
          start = nl + 1;
        }
      } else {
        // show nothing; s7 printed nothing and all results were unspecified
      }
    }

    void Draw(const char* title, bool* p_open = nullptr) {
      if (p_open) Open = *p_open;
      if (!Open) return;

      ImGui::SetNextWindowSize(ImVec2(700, 400), ImGuiCond_FirstUseEver);
      if (!ImGui::Begin(title, p_open)) {
        ImGui::End();
        return;
      }

      // Menu / Buttons
      if (ImGui::BeginPopupContextItem("console-context")) {
        if (ImGui::MenuItem("Clear")) ClearLog();
        ImGui::EndPopup();
      }

      // Scroll region
      ImGui::Separator();
      ImGui::BeginChild("scrolling", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false,
                        ImGuiWindowFlags_HorizontalScrollbar);
      for (int i = 0; i < Items.Size; i++) {
        const char* item = Items[i];
        ImGui::TextUnformatted(item);
      }
      if (ScrollToBottom) ImGui::SetScrollHereY(1.0f);
      ScrollToBottom = false;
      ImGui::EndChild();
      ImGui::Separator();

      // Input line
      bool reclaim_focus = false;
      ImGuiInputTextFlags input_flags =
          ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory;

      auto callback = [](ImGuiInputTextCallbackData* data) -> int {
        SchemeConsole* console = (SchemeConsole*)data->UserData;
        switch (data->EventFlag) {
          case ImGuiInputTextFlags_CallbackHistory: {
            const int prev_pos = console->HistoryPos;
            if (data->EventKey == ImGuiKey_UpArrow) {
              if (console->HistoryPos == -1)
                console->HistoryPos = console->History.Size - 1;
              else if (console->HistoryPos > 0)
                console->HistoryPos--;
            } else if (data->EventKey == ImGuiKey_DownArrow) {
              if (console->HistoryPos != -1)
                if (++console->HistoryPos >= console->History.Size) console->HistoryPos = -1;
            }
            // Apply history item
            if (prev_pos != console->HistoryPos) {
              const char* history_str =
                  (console->HistoryPos >= 0) ? console->History[console->HistoryPos] : "";
              data->DeleteChars(0, data->BufTextLen);
              data->InsertChars(0, history_str);
            }
          } break;
        }
        return 0;
      };

      ImGui::TextUnformatted(Prompt.c_str());
      ImGui::SameLine();
      if (ImGui::InputText("##Input", InputBuf, IM_ARRAYSIZE(InputBuf), input_flags, callback,
                           (void*)this)) {
        char* s = InputBuf;
        // Trim leading/trailing spaces
        while (*s && isspace((unsigned char)*s))
          s++;
        char* end = s + strlen(s);
        while (end > s && isspace((unsigned char)end[-1]))
          --end;
        *end = 0;

        if (*s) ExecCommand(s);
        strcpy(InputBuf, "");
        reclaim_focus = true;
      }

      // Auto-focus on window apparition
      ImGui::SetItemDefaultFocus();
      if (reclaim_focus) ImGui::SetKeyboardFocusHere(-1);  // focus back to input

      ImGui::End();
    }
  };

}  // namespace ren