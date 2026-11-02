#pragma once

// This file exposes a larger set of ImGui functionality with some custom UIs
//

// This file contains "editor ui" functionality.
// Basically, it is a set of wrappers around ImGui.

#include <imgui/imgui.h>
#include <ren/assets/AssetManager.h>
#include <ren/core/Color.h>

#include "./IconsLucide.h"


namespace ren::eui {

  namespace detail {
    // Returns the editor's monospace font, or null before configuration.
    ImFont *getMonoFont();
  }  // namespace detail


  static constexpr const char *__x = ICON_SAVE;

  enum class ButtonKind {
    Plain,    // Normal button, gray.
    Primary,  // Primary color.
    Danger,   // Red.
  };


  inline const ImVec4 primaryColor = Color(0x56759A);

  struct Style {
    std::optional<ImVec4> fg, bg;
  };

  bool ExtendedButton(const char *label, const char *icon, const Style &style = {}, ImVec2 size_arg = ImVec2(0, 0));


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
                              .fg = Color(0x6D5700),
                              .bg = COLOR_YELLOW,
                          });
  }


  inline bool Button(const char *label) { return eui::ExtendedButton(label, nullptr, {}); }
  inline bool IconButton(const char *icon) { return eui::ExtendedButton(nullptr, icon, {}); }

  void ShadowedText(const char *text);

  // Draw printf-style text with the current font.
  void Text(const char *fmt, ...) IM_FMTARGS(1);

  // Draw printf-style text with the monospace font, then restore the current font.
  void TextMono(const char *fmt, ...) IM_FMTARGS(1);

  // Same as ImGui::BeginTabBar, but draws the tab strip background so that the tabs look like docked tabs.
  // Call ImGui::EndTabBar when it returns true.
  bool BeginTabBar(const char *id, ImGuiTabBarFlags flags = 0);




  // Editor color palette as sRGB hex values. The defaults approximate the Unity dark theme.
  // applyTheme() maps these fields onto the ImGui style colors.
  struct Theme {
    // Text
    u32 text = 0xD2D2D2;
    u32 textDisabled = 0x808080;

    // Panels
    u32 windowBg = 0x151515;
    u32 popupBg = 0x2D2D2D;
    u32 border = 0x1A1A1A;

    // Tabs. The selected tab uses windowBg.
    u32 tabBarBg = 0x101010;
    u32 tabHovered = 0x303030;
    float tabOverlineSize = 2.0f;

    // Input fields, checkboxes, and drag fields
    u32 inputBg = 0x080808;
    u32 inputBgHovered = 0x2E2E2E;
    u32 inputBgActive = 0x232323;

    // Buttons and dropdowns
    u32 button = 0x585858;
    u32 buttonHovered = 0x676767;
    u32 buttonActive = 0x464646;

    // Collapsing headers and tree nodes
    u32 header = 0x3E3E3E;
    u32 headerHovered = 0x474747;

    // Focus, selection, and highlights
    u32 accent = 0x3A79BB;
    u32 selection = 0x2C5D87;

    u32 scrollbarGrab = 0x5F5F5F;
    u32 scrollbarGrabHovered = 0x6E6E6E;

    float rounding = 3.0f;
  };


  // Write the theme into the current ImGui style. You can call it again at runtime to change the theme.
  void applyTheme(const Theme &theme);

  // Configure the look of ImGui for the ren editor.
  void configure();
}  // namespace ren::eui
