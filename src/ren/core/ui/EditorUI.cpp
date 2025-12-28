#define IMGUI_DEFINE_MATH_OPERATORS
#include <ren/core/ui/EditorUI.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <ren/core/Application.h>

namespace ren::eui {

  static ImFont *iconFont = nullptr;


  bool ExtendedButton(const char *label, const char *icon, const Style &buttonStyle,
                      ImVec2 size_arg) {
    constexpr float iconFontSize = 15.0f;
    bool showIcon = (iconFont && icon && icon[0]);
    bool showLabel = (label && label[0]);

    if (!showIcon && !showLabel) {
      // Nothing to render
      return false;
    }


    ImVec4 bgColor = buttonStyle.bg.value_or(COLOR_FRAMEBG);
    bgColor.w = 1.0f;  // ensure opaque background

    ImVec4 fgColor = buttonStyle.fg.value_or(COLOR_FRAMEFG);
    fgColor.w = 1.0f;  // ensure opaque foreground

    ImGuiWindow *window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

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
    ImVec2 size = ImGui::CalcItemSize(size_arg, text_size.x + style.FramePadding.x * 2.0f,
                                      text_size.y + style.FramePadding.y * 2.0f);

    ImRect bb(window->DC.CursorPos,
              ImVec2(window->DC.CursorPos.x + size.x, window->DC.CursorPos.y + size.y));
    ImGui::ItemSize(size, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) return false;

    // Handle interaction
    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

    if (held) bgColor = bgColor + ImVec4(0.1f, 0.1f, 0.1f, 0.0f);
    if (hovered) bgColor = bgColor + ImVec4(0.1f, 0.1f, 0.1f, 0.0f);

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
    ImGui::RenderFrame(bb.Min + ImVec2(-expand, -expand), bb.Max + ImVec2(expand, expand),
                       ImGui::ColorConvertFloat4ToU32(bgColor), true, style.FrameRounding);

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
        return io.Fonts->AddFontFromMemoryTTF(fontBytes.data(), static_cast<int>(fontBytes.size()),
                                              size, &fontConfig);
      } else {
        ren::warnln("Failed to load font '{}' from asset manager!", path);
      }
      return nullptr;
    };

    loadAndMergeFont("fonts/MapleMono-Medium.ttf", 11.0, false);
    // loadAndMergeFont("fonts/Geist-Medium.ttf", 11.0, false);
    iconFont = loadAndMergeFont("fonts/lucide.ttf", 10.0, false);
  }



  void configure() {
    ImGuiStyle &style = ImGui::GetStyle();
    // ImVec4 *colors = style.Colors;
    ImVec4 *colors = ImGui::GetStyle().Colors;

    auto primaryColor = hexImColor(0x008540);

    auto windowBackground = ImVec4(0.001f, 0.001f, 0.001f, 1.00f);
    auto lighten = [](const ImVec4 &color, float amount) {
      return ImVec4(color.x + amount, color.y + amount, color.z + amount, color.w);
    };


    colors[ImGuiCol_CheckMark] = primaryColor;
    colors[ImGuiCol_SliderGrab] = primaryColor;
    colors[ImGuiCol_SliderGrabActive] = primaryColor;
    colors[ImGuiCol_SeparatorHovered] = primaryColor;
    colors[ImGuiCol_SeparatorActive] = primaryColor;
    colors[ImGuiCol_ResizeGrip] = primaryColor;
    colors[ImGuiCol_ResizeGripHovered] = primaryColor;
    colors[ImGuiCol_ResizeGripActive] = primaryColor;
    colors[ImGuiCol_TextLink] = primaryColor;
    colors[ImGuiCol_NavCursor] = primaryColor;


    colors[ImGuiCol_MenuBarBg] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.11f, 0.11f, 0.11f, 1.00f);
    colors[ImGuiCol_MenuBarBg] = windowBackground;
    colors[ImGuiCol_ScrollbarBg] = lighten(windowBackground, 0.02f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.09f, 0.09f, 0.09f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.67f, 0.67f, 0.67f, 1.00f);

    colors[ImGuiCol_Header] = ImVec4(0.02f, 0.02f, 0.02f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.05f, 0.05f, 0.05f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_Tab] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TabSelected] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.16f, 0.16f, 0.16f, 0.80f);
    colors[ImGuiCol_TabSelectedOverline] = ImVec4(1.00f, 1.00f, 1.00f, 0.0f);
    colors[ImGuiCol_TabDimmed] = windowBackground;
    colors[ImGuiCol_TabDimmedSelected] = lighten(windowBackground, 0.02f);
    colors[ImGuiCol_DockingPreview] = ImVec4(0.98f, 0.99f, 1.00f, 0.09f);
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TableHeaderBg] = hexImColor(0x030303);
    colors[ImGuiCol_TableBorderStrong] = lighten(windowBackground, 0.02f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.07f, 0.07f, 0.07f, 1.00f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.03f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.29f, 0.29f, 0.29f, 0.06f);
    colors[ImGuiCol_TreeLines] = ImVec4(0.29f, 0.29f, 0.31f, 0.50f);




    // Window and Child backgrounds.
    colors[ImGuiCol_WindowBg] = windowBackground;
    colors[ImGuiCol_ChildBg] = windowBackground;
    colors[ImGuiCol_Border] = lighten(windowBackground, 0.05f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_FrameBg] = COLOR_FRAMEBG;
    colors[ImGuiCol_FrameBgHovered] = colors[ImGuiCol_FrameBg] + ImVec4(0.05f, 0.05f, 0.05f, 0.0f);
    colors[ImGuiCol_FrameBgActive] = colors[ImGuiCol_FrameBg] + ImVec4(0.10f, 0.10f, 0.10f, 0.0f);

    // Collapsable Headers.
    colors[ImGuiCol_TitleBg] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = windowBackground;
    colors[ImGuiCol_TitleBgCollapsed] = windowBackground;

    colors[ImGuiCol_ScrollbarBg] = windowBackground;
    colors[ImGuiCol_Button] = ImVec4(0.02f, 0.02f, 0.02f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.09f, 0.09f, 0.09f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.03f, 0.03f, 0.03f, 1.00f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
    colors[ImGuiCol_TableRowBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.10f, 0.10f, 0.10f, 0.06f);




    style.TabRounding = 0.0f;
    style.WindowMenuButtonPosition = ImGuiDir_None;
    style.FontSizeBase = 15.0f;
    style.DockingSeparatorSize = 4.0f;
    style.FrameRounding = 4.0f;
    // style.FramePadding = ImVec2(8.0f, 2.0f);
    // style.FrameBorderSize = 2.0f;
    // style.WindowRounding = 5.0f;

    style.WindowPadding = ImVec2(10.0f, 2.0f);


    auto &am = ren::ensureResource<ren::AssetManager>();

    loadRenFonts(am);
  }
}  // namespace ren::eui