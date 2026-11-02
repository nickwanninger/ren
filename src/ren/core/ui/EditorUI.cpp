#define IMGUI_DEFINE_MATH_OPERATORS
#include <ren/core/ui/EditorUI.h>

#include <cstdarg>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imnodes/imnodes.h>
#include <ren/core/Application.h>

namespace ren::eui {

  static ImFont *iconFont = nullptr;
  static ImFont *defaultFont = nullptr;
  static ImFont *monoFont = nullptr;


  ImFont *detail::getMonoFont() { return monoFont; }


  bool ExtendedButton(const char *label, const char *icon, const Style &buttonStyle, ImVec2 size_arg) {
    constexpr float iconFontSize = 15.0f;
    bool showIcon = (iconFont && icon && icon[0]);
    bool showLabel = (label && label[0]);

    if (!showIcon && !showLabel) {
      // Nothing to render
      return false;
    }


    ImVec4 bgColor = buttonStyle.bg.value_or(ImGui::GetStyleColorVec4(ImGuiCol_Button));
    bgColor.w = 1.0f;  // ensure opaque background

    ImVec4 fgColor = buttonStyle.fg.value_or(ImGui::GetStyleColorVec4(ImGuiCol_Text));
    fgColor.w = 1.0f;  // ensure opaque foreground

    ImGuiWindow *window = ImGui::GetCurrentWindow();
    if (window->SkipItems) {
      return false;
    }

    ImGuiContext &g = *GImGui;
    const ImGuiStyle &style = g.Style;
    const ImGuiID id = window->GetID(showLabel ? label : icon);


    // Calculate text sizes
    ImVec2 icon_size, label_size;
    if (showIcon) {
      ImGui::PushFont(iconFont);
      ImGui::PushFontSize(iconFontSize);
      icon_size = ImGui::CalcTextSize(icon);
      ImGui::PopFontSize();
      ImGui::PopFont();
    } else {
      icon_size = ImVec2(0, 0);
    }


    if (showLabel) {
      label_size = ImGui::CalcTextSize(label);
    } else {
      label_size = ImVec2(0, 0);
    }

    const float spacing = (showIcon && showLabel) ? style.ItemInnerSpacing.x : 0.0f;
    const ImVec2 text_size(icon_size.x + spacing + label_size.x, ImMax(icon_size.y, label_size.y));

    // Calculate button size
    ImVec2 size = ImGui::CalcItemSize(size_arg, text_size.x + style.FramePadding.x * 2.0f, text_size.y + style.FramePadding.y * 2.0f);

    ImRect bb(window->DC.CursorPos, ImVec2(window->DC.CursorPos.x + size.x, window->DC.CursorPos.y + size.y));
    ImGui::ItemSize(size, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) {
      return false;
    }

    // Handle interaction
    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

    if (!buttonStyle.bg) {
      bgColor = ImGui::GetStyleColorVec4(held ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
    } else if (held || hovered) {
      bgColor = bgColor + ImVec4(0.1f, 0.1f, 0.1f, 0.0f);
    }

    float expand = 0.0f;

    if (held) {
      expand = -0.1f;
    } else if (hovered) {
      // expand = 2.0f;
      bb.Min.y -= 1.0f;
      bb.Max.y -= 1.0f;
    }


    // bb.Min += ImVec2(-expand, -expand);
    // bb.Max += ImVec2(expand, expand);
    // if (held) {Vk
    //   bb.Min.y += 1.0f;
    //   bb.Max.y += 1.0f;
    // } else if (hovered) {
    //   bb.Min.y -= 1.0f;
    //   bb.Max.y -= 1.0f;
    // }


    ImGui::RenderNavHighlight(bb, id);
    ImGui::RenderFrame(bb.Min + ImVec2(-expand, -expand), bb.Max + ImVec2(expand, expand), ImGui::ColorConvertFloat4ToU32(bgColor), true,
                       style.FrameRounding);

    // Render icon and text
    ImVec2 text_pos = bb.Min + style.FramePadding;
    text_pos.x += (size.x - style.FramePadding.x * 2.0f - text_size.x) * 0.5f;
    text_pos.y += (size.y - style.FramePadding.y * 2.0f - text_size.y) * 0.5f;

    u32 textColor = ImGui::ColorConvertFloat4ToU32(fgColor);

    if (showIcon) {
      ImGui::PushFont(iconFont);
      ImGui::PushFontSize(iconFontSize);
      auto icon_pos = text_pos - ImVec2(0.0f, 1.0f);  // slight vertical adjustment
      // window->DrawList->AddRect(icon_pos, icon_pos + icon_size, textColor);
      window->DrawList->AddText(icon_pos, textColor, icon);
      ImGui::PopFontSize();
      ImGui::PopFont();
      text_pos.x += icon_size.x + spacing;
    }
    if (showLabel) {
      // window->DrawList->AddRect(text_pos, text_pos + label_size, textColor);
      window->DrawList->AddText(text_pos, textColor, label);
    }

    return pressed;
  }


  bool BeginTabBar(const char *id, ImGuiTabBarFlags flags) {
    // Docked windows draw a strip behind their tabs, but plain tab bars do not.
    // Without the strip, the selected tab blends into the window and the unselected tabs look like buttons.
    // This rectangle matches the tab bar rectangle in ImGui::BeginTabBar.
    ImGuiWindow *window = ImGui::GetCurrentWindow();
    if (!window->SkipItems) {
      ImVec2 min = window->DC.CursorPos;
      ImVec2 max = ImVec2(window->WorkRect.Max.x, min.y + ImGui::GetFrameHeight());
      window->DrawList->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_TitleBg), ImGui::GetStyle().TabRounding, ImDrawFlags_RoundCornersTop);
    }

    return ImGui::BeginTabBar(id, flags | ImGuiTabBarFlags_DrawSelectedOverline);
  }


  void ShadowedText(const char *text) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList *drawList = ImGui::GetWindowDrawList();

    ImVec2 shadowOffset = ImVec2(0.0f, 1.5f);

    // Draw shadow
    drawList->AddText(ImVec2(pos.x + shadowOffset.x, pos.y + shadowOffset.y), ImGui::ColorConvertFloat4ToU32(ImVec4(0, 0, 0, 1)), text);


    // get the text color from the current style
    ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    // Draw actual text
    drawList->AddText(pos, ImGui::ColorConvertFloat4ToU32(textColor), text);

    // Advance cursor (use CalcTextSize if you need precise layout)
    ImGui::Dummy(ImGui::CalcTextSize(text));
  }

  void Text(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ImGui::TextV(fmt, args);
    va_end(args);
  }


  void TextMono(const char *fmt, ...) {
    ImGui::PushFont(monoFont);

    va_list args;
    va_start(args, fmt);
    ImGui::TextV(fmt, args);
    va_end(args);

    ImGui::PopFont();
  }


  void loadRenFonts(ren::AssetManager &am) {
    auto &io = ImGui::GetIO();
    auto loadAndMergeFont = [&](const char *path, float size, bool merge) -> ImFont * {
      std::vector<u8> fontBytes;
      if (am.load(path, fontBytes)) {
        ImFontConfig fontConfig;
        fontConfig.MergeMode = merge;
        fontConfig.OversampleH = 3;
        fontConfig.OversampleV = 1;
        fontConfig.PixelSnapH = true;
        fontConfig.FontDataOwnedByAtlas = false;
        fontConfig.SizePixels = size;
        return io.Fonts->AddFontFromMemoryTTF(fontBytes.data(), static_cast<int>(fontBytes.size()), size, &fontConfig);
      } else {
        ren::warnln("Failed to load font '{}' from asset manager!", path);
      }
      return nullptr;
    };

    monoFont = loadAndMergeFont("fonts/MapleMono-Medium.ttf", 11.0, false);
    defaultFont = loadAndMergeFont("fonts/Inter-Regular.ttf", 13.0, false);
    iconFont = loadAndMergeFont("fonts/lucide.ttf", 10.0, false);
    io.FontDefault = defaultFont;
  }



  void applyTheme(const Theme &theme) {
    ImGuiStyle &style = ImGui::GetStyle();
    ImVec4 *colors = style.Colors;


    // Reset first so every slot holds an sRGB value before the linear conversion below.
    // This makes repeated calls safe.
    ImGui::StyleColorsDark(&style);


    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.TabBorderSize = 0.0f;
    style.TabBarBorderSize = 1.0f;
    style.TabBarOverlineSize = theme.tabOverlineSize;

    style.WindowRounding = 0.0f;
    style.ChildRounding = theme.rounding;
    style.FrameRounding = theme.rounding;
    style.PopupRounding = theme.rounding;
    style.GrabRounding = theme.rounding;
    style.TabRounding = theme.rounding;
    style.ScrollbarRounding = 6.0f;

    style.WindowPadding = ImVec2(6.0f, 6.0f);
    // ImGui sizes tabs from FramePadding, so this also sets the padding around tab titles.
    style.FramePadding = ImVec2(5.0f, 3.0f);
    style.ItemSpacing = ImVec2(6.0f, 4.0f);
    style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);

    style.FontSizeBase = 14.0f;


    auto c = [](u32 hex, float alpha = 1.0f) { return ren::hexImColor(hex, alpha); };

    colors[ImGuiCol_Text] = c(theme.text);
    colors[ImGuiCol_TextDisabled] = c(theme.textDisabled);
    colors[ImGuiCol_TextSelectedBg] = c(theme.selection, 0.6f);
    colors[ImGuiCol_TextLink] = c(theme.accent);

    colors[ImGuiCol_WindowBg] = c(theme.windowBg);
    colors[ImGuiCol_ChildBg] = c(0, 0.0f);
    colors[ImGuiCol_PopupBg] = c(theme.popupBg);
    colors[ImGuiCol_MenuBarBg] = c(theme.tabBarBg);
    colors[ImGuiCol_Border] = c(theme.border);
    colors[ImGuiCol_BorderShadow] = c(0, 0.0f);

    // Input fields, checkboxes, sliders, and drag fields.
    colors[ImGuiCol_FrameBg] = c(theme.inputBg);
    colors[ImGuiCol_FrameBgHovered] = c(theme.inputBgHovered);
    colors[ImGuiCol_FrameBgActive] = c(theme.inputBgActive);
    colors[ImGuiCol_InputTextCursor] = c(theme.text);
    colors[ImGuiCol_CheckMark] = c(theme.text);
    colors[ImGuiCol_SliderGrab] = c(theme.button);
    colors[ImGuiCol_SliderGrabActive] = c(theme.accent);

    // Docked windows draw their tab strip with the title colors.
    colors[ImGuiCol_TitleBg] = c(theme.tabBarBg);
    colors[ImGuiCol_TitleBgActive] = c(theme.tabBarBg);
    colors[ImGuiCol_TitleBgCollapsed] = c(theme.tabBarBg);

    // The selected tab uses the window color so that it joins the panel below it.
    colors[ImGuiCol_Tab] = c(theme.tabBarBg);
    colors[ImGuiCol_TabHovered] = c(theme.tabHovered);
    colors[ImGuiCol_TabSelected] = c(theme.windowBg);
    colors[ImGuiCol_TabSelectedOverline] = c(theme.accent);
    colors[ImGuiCol_TabDimmed] = c(theme.tabBarBg);
    colors[ImGuiCol_TabDimmedSelected] = c(theme.windowBg);
    colors[ImGuiCol_TabDimmedSelectedOverline] = c(0, 0.0f);

    colors[ImGuiCol_Button] = c(theme.button);
    colors[ImGuiCol_ButtonHovered] = c(theme.buttonHovered);
    colors[ImGuiCol_ButtonActive] = c(theme.buttonActive);

    // ImGui uses the header colors for collapsing headers, tree nodes, and selectables.
    colors[ImGuiCol_Header] = c(theme.header);
    colors[ImGuiCol_HeaderHovered] = c(theme.headerHovered);
    colors[ImGuiCol_HeaderActive] = c(theme.selection);

    colors[ImGuiCol_Separator] = c(theme.border);
    colors[ImGuiCol_SeparatorHovered] = c(theme.accent, 0.7f);
    colors[ImGuiCol_SeparatorActive] = c(theme.accent);
    colors[ImGuiCol_ResizeGrip] = c(0, 0.0f);
    colors[ImGuiCol_ResizeGripHovered] = c(theme.accent, 0.7f);
    colors[ImGuiCol_ResizeGripActive] = c(theme.accent);

    colors[ImGuiCol_ScrollbarBg] = c(theme.windowBg);
    colors[ImGuiCol_ScrollbarGrab] = c(theme.scrollbarGrab);
    colors[ImGuiCol_ScrollbarGrabHovered] = c(theme.scrollbarGrabHovered);
    colors[ImGuiCol_ScrollbarGrabActive] = c(theme.scrollbarGrabHovered);

    colors[ImGuiCol_DockingPreview] = c(theme.accent, 0.5f);
    colors[ImGuiCol_DockingEmptyBg] = c(theme.tabBarBg);

    colors[ImGuiCol_TableHeaderBg] = c(theme.header);
    colors[ImGuiCol_TableBorderStrong] = c(theme.border);
    colors[ImGuiCol_TableBorderLight] = c(theme.header);
    colors[ImGuiCol_TableRowBg] = c(0, 0.0f);
    colors[ImGuiCol_TableRowBgAlt] = c(0xFFFFFF, 0.03f);

    colors[ImGuiCol_DragDropTarget] = c(theme.accent);
    colors[ImGuiCol_NavCursor] = c(theme.accent);


    // The palette is sRGB. The ImGui render target is sRGB, so convert to linear.
    auto srgbToLinear = [](const ImVec4 &c) -> ImVec4 {
      auto toLinear = [](float x) -> float { return (x <= 0.04045f) ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f); };
      return ImVec4(toLinear(c.x), toLinear(c.y), toLinear(c.z), c.w);
    };
    for (int i = 0; i < ImGuiCol_COUNT; i++) {
      colors[i] = srgbToLinear(colors[i]);
    }
  }


  void configure() {
    applyTheme(Theme{});

    auto &am = ren::Application::get().getAssetManager();


    loadRenFonts(am);
  }
}  // namespace ren::eui
