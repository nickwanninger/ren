#define IMGUI_DEFINE_MATH_OPERATORS
#include <ren/core/ui/EditorUI.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imnodes/imnodes.h>
#include <ren/core/Application.h>

namespace ren::eui {

  static ImFont *iconFont = nullptr;


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

    loadAndMergeFont("fonts/MapleMono-Medium.ttf", 11.0, false);
    // loadAndMergeFont("fonts/Geist-Medium.ttf", 11.0, false);
    iconFont = loadAndMergeFont("fonts/lucide.ttf", 10.0, false);
  }



  void configure() {
    ImGuiStyle &style = ImGui::GetStyle();
    ImVec4 *colors = style.Colors;


    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;

    style.WindowRounding = 2.0f;
    style.ChildRounding = 2.0f;
    style.FrameRounding = 2.0f;
    style.PopupRounding = 2.0f;
    style.GrabRounding = 2.0f;

    style.FontSizeBase = 13.0f;



    auto srgbToLinear = [](const ImVec4 &c) -> ImVec4 {
      auto toLinear = [](float x) -> float { return (x <= 0.04045f) ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f); };
      return ImVec4(toLinear(c.x), toLinear(c.y), toLinear(c.z), c.w);
    };
    // go through the colors and srgb adjust them.
    for (int i = 0; i < ImGuiCol_COUNT; i++) {
      colors[i] = srgbToLinear(colors[i]);
    }

    auto &am = ren::ensureResource<ren::AssetManager>();


    // loadRenFonts(am);
  }
}  // namespace ren::eui
