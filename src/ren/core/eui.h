#pragma once

// This file exposes a larger set of ImGui functionality with some custom UIs
//

// This file contains "editor ui" functionality.
// Basically, it is a set of wrappers around ImGui.

#include <imgui/imgui.h>
#include <ren/assets/AssetManager.h>
#include <ren/core/Color.h>

#include "ui/IconsLucide.h"


namespace ren::eui {


  enum class ButtonKind {
    Plain,    // Normal button, gray.
    Primary,  // Primary color.
    Danger,   // Red.
  };


  constexpr ImVec4 primaryColor = ImVec4(0.0f, 0.52f, 0.25f, 1.0f);

  // namespace colors {
  //   // Opinionated colors
  //   Color white = Color(0xFFFFFF);
  //   Color red = Color(0xFFF5C60);
  //   Color yellow = Color(0xFAC800);
  //   Color green = Color(0x35C759);
  //   Color blue = Color(0x9DCEFF);

  //   Color frameBackground = Color(0x030303);
  //   Color frameForeground = white;
  // }  // namespace colors

  struct Style {
    std::optional<ImVec4> fg, bg;
  };

  bool ExtendedButton(const char *label, const char *icon, const Style &style = {},
                      ImVec2 size_arg = ImVec2(0, 0));


  inline bool ButtonGreen(const char *label, const char *icon = nullptr) {
    return ExtendedButton(label, icon,
                          {
                              .bg = COLOR_GREEN,
                              // .fg = hexColor(0x082B11),
                          });
  }
  inline bool ButtonRed(const char *label, const char *icon = nullptr) {
    return ExtendedButton(label, icon,
                          {
                              .bg = COLOR_RED,
                              // .fg = hexColor(0x720104),
                          });
  }


  inline bool ButtonYellow(const char *label, const char *icon = nullptr) {
    return ExtendedButton(label, icon,
                          {
                              .bg = COLOR_YELLOW,
                              .fg = Color(0x6D5700),
                          });
  }




  // Configure the look of ImGui for the ren editor.
  void configure();
}  // namespace ren::eui