#include <ren/core/ui/Widgets.h>
#include <ren/core/ui/EditorUI.h>

#include <imgui_internal.h>
#include <cstdarg>
#include <cstdio>

namespace ren::eui {

  Ui::Ui(bool values)
      : context(ImGui::GetCurrentContext())
      , window(ImGui::GetCurrentWindow())
      , tableId(values ? ImGui::GetCurrentTable()->ID : 0)
      , values(values) {}


  void Ui::validate() const {
    IM_ASSERT(context == ImGui::GetCurrentContext());
    IM_ASSERT(window == ImGui::GetCurrentWindow());
    IM_ASSERT(!values || (ImGui::GetCurrentTable() && tableId == ImGui::GetCurrentTable()->ID));
  }


  bool Ui::beginTable(const char* id) {
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, ImGui::GetFontSize());

    const auto flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBody;
    if (!ImGui::BeginTable(id, 2, flags)) {
      ImGui::PopStyleVar(4);
      return false;
    }

    ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_IndentEnable, 0.38f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_IndentDisable, 0.62f);
    return true;
  }


  void Ui::endTable() {
    ImGui::EndTable();
    ImGui::PopStyleVar(4);
  }


  bool Ui::beginGroup(const char* label, bool* enabled) {
    validate();

    // Initial table layout can submit items before the header consumes SetNextItemOpen().
    const auto nextItemData = GImGui->NextItemData;
    ImGui::PushID(label);
    if (values) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
    }

    const auto flags = ImGuiTreeNodeFlags_Framed | (values ? ImGuiTreeNodeFlags_SpanAllColumns : 0) | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                       ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap;
    const auto padding = ImGui::GetStyle().FramePadding;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padding.x, std::max(padding.y, 4.0f)));
    GImGui->NextItemData = nextItemData;
    const bool open = ImGui::TreeNodeEx("##header", flags);
    const auto headerMin = ImGui::GetItemRectMin();
    const auto& style = ImGui::GetStyle();
    float labelX = headerMin.x + ImGui::GetTreeNodeToLabelSpacing();

    // Header controls and text span both columns, like the header background.
    const auto clip = values ? ImGui::GetCurrentTable()->InnerClipRect : ImGui::GetCurrentWindow()->ClipRect;
    ImGui::PushClipRect(clip.Min, clip.Max, false);
    if (enabled) {
      const float height = ImGui::GetFontSize();
      const auto position = ImVec2(labelX + style.ItemInnerSpacing.x, headerMin.y + style.FramePadding.y);
      const ImRect bounds(position, ImVec2(position.x + height * 1.8f, position.y + height));
      const ImGuiID id = ImGui::GetID("##enabled");
      // The switch overlays the header. It must not change line sizes or cursor positions.
      if (ImGui::ItemAdd(bounds, id)) {
        bool hovered, held;
        if (ImGui::ButtonBehavior(bounds, id, &hovered, &held)) {
          *enabled = !*enabled;
        }
        detail::drawToggle(position, height, *enabled);
      }
      labelX = bounds.Max.x + style.ItemSpacing.x;
    }

    // Draw the label text over the header, after the switch when it exists.
    const auto text = std::string_view(label).substr(0, std::string_view(label).find("##"));
    ImGui::GetWindowDrawList()->AddText(ImVec2(labelX, headerMin.y + style.FramePadding.y), ImGui::GetColorU32(ImGuiCol_Text), text.data(),
                                        text.data() + text.size());
    ImGui::PopClipRect();
    ImGui::PopStyleVar();

    if (!open) {
      ImGui::PopID();
      return false;
    }

    ImGui::Indent();
    ImGui::BeginDisabled(enabled && !*enabled);
    return true;
  }


  void Ui::endGroup() {
    ImGui::EndDisabled();
    if (values) {
      ImGui::TableSetColumnIndex(0);
    }
    ImGui::Unindent();
    ImGui::PopID();
  }


  void Ui::beginContent(float& maxX, float& itemWidth) {
    const auto lastItem = GImGui->LastItemData;
    const auto nextItem = GImGui->NextItemData;
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);

    // Widen the work rect from the label column to the end of the value column.
    auto* currentWindow = ImGui::GetCurrentWindow();
    auto* currentTable = ImGui::GetCurrentTable();
    maxX = currentWindow->WorkRect.Max.x;
    itemWidth = currentWindow->DC.ItemWidth;
    currentWindow->WorkRect.Max.x = currentTable->Columns[1].WorkMaxX;
    currentWindow->DC.ItemWidth = ImGui::GetContentRegionAvail().x * 0.65f;
    ImGui::PushClipRect(currentTable->InnerClipRect.Min, currentTable->InnerClipRect.Max, false);

    GImGui->LastItemData = lastItem;
    GImGui->NextItemData = nextItem;
  }


  void Ui::endContent(float maxX, float itemWidth) {
    validate();

    auto* currentWindow = ImGui::GetCurrentWindow();
    ImGui::PopClipRect();
    currentWindow->WorkRect.Max.x = maxX;
    currentWindow->DC.ItemWidth = itemWidth;
    // Full-width content must not change the label column's preferred width.
    currentWindow->DC.CursorMaxPos.x = std::min(currentWindow->DC.CursorMaxPos.x, maxX);
  }


  void Ui::beginField(const char* label) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    const auto text = std::string_view(label).substr(0, std::string_view(label).find("##"));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text.data(), text.data() + text.size());

    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-1.0f);
  }


  void Ui::text(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    content([&](Ui&) { ImGui::TextV(fmt, args); });
    va_end(args);
  }


  void Ui::textMono(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    content([&](Ui&) {
      ImGui::PushFont(detail::getMonoFont());
      detail::ScopeExit finish([] { ImGui::PopFont(); });
      ImGui::TextV(fmt, args);
    });
    va_end(args);
  }


  void Ui::textUnformatted(const char* text) {
    content([&](Ui&) { ImGui::TextUnformatted(text); });
  }


  void Ui::separator(const char* label) {
    content([&](Ui&) {
      if (label) {
        ImGui::SeparatorText(label);
      } else {
        ImGui::Separator();
      }
    });
  }


  void Ui::image(ImTextureRef texture, ImVec2 size, ImVec2 uv0, ImVec2 uv1) {
    content([&](Ui&) { ImGui::Image(texture, size, uv0, uv1); });
  }


  bool Ui::button(const char* label, const char* icon, ImVec2 size) {
    return content([&](Ui&) { return ExtendedButton(label, icon, {}, size); });
  }


  void Ui::sameLine(float offset, float spacing) {
    validate();
    ImGui::SameLine(offset, spacing);
  }


  bool Ui::beginTabBar(const char* id, ImGuiTabBarFlags flags) { return BeginTabBar(id, flags); }



  //=========================
  // Blocks
  //=========================

  const BlockStyle& getBlockStyle() {
    static const BlockStyle style;
    return style;
  }


  void drawBlock(ImDrawList* drawList, ImVec2 min, ImVec2 max, BlockKind kind, bool hovered, bool active, ImDrawFlags corners) {
    const auto& style = getBlockStyle();

    ImGuiCol color;
    if (kind == BlockKind::Raised) {
      color = active ? style.raisedActive : hovered ? style.raisedHovered : style.raised;
    } else {
      color = active ? style.loweredActive : hovered ? style.loweredHovered : style.lowered;
    }

    // The backdrop line follows the bottom edge and its round corners. Draw it first, so the
    // block hides the top of the corner arcs.
    if ((corners & ImDrawFlags_RoundCornersMask_) == 0) {
      corners = ImDrawFlags_RoundCornersAll;
    }
    const float y = max.y + style.backdropOffset;
    drawList->PathClear();
    if (corners & ImDrawFlags_RoundCornersBottomLeft) {
      drawList->PathArcTo(ImVec2(min.x + style.rounding, y - style.rounding), style.rounding, IM_PI, IM_PI * 0.5f);
    } else {
      drawList->PathLineTo(ImVec2(min.x, y));
    }
    if (corners & ImDrawFlags_RoundCornersBottomRight) {
      drawList->PathArcTo(ImVec2(max.x - style.rounding, y - style.rounding), style.rounding, IM_PI * 0.5f, 0.0f);
    } else {
      drawList->PathLineTo(ImVec2(max.x, y));
    }
    drawList->PathStroke(ImGui::GetColorU32(style.backdrop), ImDrawFlags_None, 1.0f);

    drawList->AddRectFilled(min, max, ImGui::GetColorU32(color), style.rounding, corners);
  }


  bool block(const char* label, const BlockOptions& options) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) {
      return false;
    }

    const auto& style = getBlockStyle();
    const ImGuiID id = window->GetID(label);
    const ImVec2 size = ImGui::CalcItemSize(options.size, ImGui::CalcItemWidth(), ImGui::GetFrameHeight());
    const ImVec2 position = window->DC.CursorPos;
    const ImRect bounds(position, ImVec2(position.x + size.x, position.y + size.y));
    ImGui::ItemSize(size, ImGui::GetStyle().FramePadding.y);
    if (!ImGui::ItemAdd(bounds, id)) {
      return false;
    }

    bool hovered, held;
    const bool pressed = ImGui::ButtonBehavior(bounds, id, &hovered, &held);

    ImGui::RenderNavCursor(bounds, id);
    drawBlock(window->DrawList, bounds.Min, bounds.Max, options.kind, hovered, held, options.corners);

    const ImRect textBounds(ImVec2(bounds.Min.x + style.textPadding, bounds.Min.y), ImVec2(bounds.Max.x - style.textPadding, bounds.Max.y));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(options.textColor));
    ImGui::RenderTextClipped(textBounds.Min, textBounds.Max, label, nullptr, nullptr, ImVec2(options.textAlign, 0.5f), &bounds);
    ImGui::PopStyleColor();

    return pressed;
  }



  //=========================
  // Number input
  //=========================

  // ImGui keeps this value private in imgui_widgets.cpp. Drags react before the normal drag threshold.
  static constexpr float dragThresholdFactor = 0.5f;


  // Return true when the mouse moved far enough since the click to count as a drag.
  static bool isDragging() { return ImGui::IsMouseDragPastThreshold(0, ImGui::GetIO().MouseDragThreshold * dragThresholdFactor); }


  // Handle activation for a number block, like the start of ImGui::DragScalar. A click makes the
  // block active; Ctrl+click or keyboard activation asks for text input. Return true for text input.
  static bool activateNumber(ImGuiID id, const ImRect& frame, ImGuiDataType dataType, void* data, bool inputAllowed, bool& hovered) {
    ImGuiContext& g = *GImGui;
    hovered = ImGui::ItemHoverable(frame, id, g.LastItemData.ItemFlags);
    if (inputAllowed && ImGui::TempInputIsActive(id)) {
      return true;
    }

    const bool clicked = hovered && ImGui::IsMouseClicked(0, ImGuiInputFlags_None, id);
    const bool makeActive = clicked || g.NavActivateId == id;
    if (makeActive && clicked) {
      ImGui::SetKeyOwner(ImGuiKey_MouseLeft, id);
    }

    bool textInput = false;
    if (makeActive && inputAllowed) {
      textInput = (clicked && g.IO.KeyCtrl) || (g.NavActivateId == id && (g.NavActivateFlags & ImGuiActivateFlags_PreferInput));
    }

    // ImGui stores the initial value for mods that revert an edit.
    if (makeActive) {
      memcpy(&g.ActiveIdValueOnActivation, data, ImGui::DataTypeGetInfo(dataType)->Size);
    }

    if (makeActive && !textInput) {
      ImGui::SetActiveID(id, ImGui::GetCurrentWindow());
      ImGui::SetFocusID(id, ImGui::GetCurrentWindow());
      ImGui::FocusWindow(ImGui::GetCurrentWindow());
      g.ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right);
    }

    return textInput;
  }


  // Return true on the frame that the mouse releases an active block without a drag.
  static bool isReleasedWithoutDrag(ImGuiID id) {
    ImGuiContext& g = *GImGui;
    return g.ActiveId == id && g.ActiveIdSource == ImGuiInputSource_Mouse && g.IO.MouseReleased[0] && !isDragging();
  }


  // Turn the block into a text field on this frame, like io.ConfigDragClickToInputText in ImGui.
  static void requestTextInput(ImGuiID id) {
    ImGuiContext& g = *GImGui;
    g.NavActivateId = id;
    g.NavActivateFlags = ImGuiActivateFlags_PreferInput;
  }


  // Draw the text field that replaces a number block during text input. It uses the lowered look.
  static bool textInputNumber(const ImRect& frame, ImGuiID id, const char* label, ImGuiDataType dataType, void* data, const char* format,
                              const void* clampMin, const void* clampMax) {
    const auto& style = getBlockStyle();
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetStyleColorVec4(style.lowered));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, style.rounding);
    const bool changed = ImGui::TempInputScalar(frame, id, label, dataType, data, format, clampMin, clampMax);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    return changed;
  }


  // Draw the label on the left and the value on the right, `inset` from each end of the block.
  static void drawNumberText(const ImRect& frame, float inset, const char* label, ImGuiDataType dataType, const void* data, const char* format) {
    const ImRect textBounds(ImVec2(frame.Min.x + inset, frame.Min.y), ImVec2(frame.Max.x - inset, frame.Max.y));
    const char* labelEnd = ImGui::FindRenderedTextEnd(label);
    if (label != labelEnd) {
      ImGui::RenderTextClipped(textBounds.Min, textBounds.Max, label, labelEnd, nullptr, ImVec2(0.0f, 0.5f), &frame);
    }

    char value[64];
    const char* valueEnd = value + ImGui::DataTypeFormatString(value, IM_ARRAYSIZE(value), dataType, data, format);
    if (GImGui->LogEnabled) {
      ImGui::LogSetNextTextDecoration("{", "}");
    }
    ImGui::RenderTextClipped(textBounds.Min, textBounds.Max, value, valueEnd, nullptr, ImVec2(1.0f, 0.5f), &frame);
  }


  // Write the ID label of a number block: the label with all text hidden. The text field
  // in TempInputScalar() draws the visible text of its label after the field, which widens the item.
  static void makeIdLabel(char (&idLabel)[128], const char* label) { ImFormatString(idLabel, sizeof(idLabel), "##%s", label); }


  // Add the item for a number block and return its frame. Return false when ImGui clips the item.
  static bool addNumberItem(const char* idLabel, bool inputAllowed, ImGuiID& id, ImRect& frame) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) {
      return false;
    }

    id = window->GetID(idLabel);
    const ImVec2 position = window->DC.CursorPos;
    frame = ImRect(position, ImVec2(position.x + ImGui::CalcItemWidth(), position.y + ImGui::GetFrameHeight()));
    ImGui::ItemSize(frame, ImGui::GetStyle().FramePadding.y);
    return ImGui::ItemAdd(frame, id, &frame, inputAllowed ? ImGuiItemFlags_Inputable : 0);
  }



  //=========================
  // Drag block
  //=========================

  // Add or subtract one step. Float types step by `speed`, integer types step by one.
  static void stepValue(ImGuiDataType dataType, void* data, float speed, int direction, const void* min, const void* max) {
    ImGuiDataTypeStorage step = {};
    if (dataType == ImGuiDataType_Float) {
      *(float*)&step = speed;
    } else if (dataType == ImGuiDataType_Double) {
      *(double*)&step = speed;
    } else {
      // Integer storage is little endian, so 1 in the first byte is 1 for every width.
      step.Data[0] = 1;
    }

    ImGui::DataTypeApplyOp(dataType, direction < 0 ? '-' : '+', data, data, &step);
    if (min || max) {
      ImGui::DataTypeClamp(dataType, data, min, max);
    }
  }


  bool dragBlock(const char* label, ImGuiDataType dataType, void* data, float speed, const void* min, const void* max, const char* format,
                 ImGuiSliderFlags flags, ImDrawFlags corners) {
    const bool inputAllowed = (flags & ImGuiSliderFlags_NoInput) == 0;
    char idLabel[128];
    makeIdLabel(idLabel, label);
    ImGuiID id;
    ImRect frame;
    if (!addNumberItem(idLabel, inputAllowed, id, frame)) {
      return false;
    }

    ImGuiContext& g = *GImGui;
    const auto& blockStyle = getBlockStyle();
    if (format == nullptr) {
      format = ImGui::DataTypeGetInfo(dataType)->PrintFmt;
    }

    // The arrow zones at each end step the value on a click without a drag.
    const ImRect leftArrow(frame.Min, ImVec2(frame.Min.x + blockStyle.arrowWidth, frame.Max.y));
    const ImRect rightArrow(ImVec2(frame.Max.x - blockStyle.arrowWidth, frame.Min.y), frame.Max);

    bool hovered;
    bool textInput = activateNumber(id, frame, dataType, data, inputAllowed, hovered);

    // A release without a drag steps the value on an arrow, and opens text input elsewhere.
    // DragBehavior() clears the active ID on release, so this check must come first.
    bool stepped = false;
    if (!textInput && hovered && isReleasedWithoutDrag(id)) {
      if (leftArrow.Contains(g.IO.MouseClickedPos[0])) {
        stepValue(dataType, data, speed, -1, min, max);
        stepped = true;
      } else if (rightArrow.Contains(g.IO.MouseClickedPos[0])) {
        stepValue(dataType, data, speed, 1, min, max);
        stepped = true;
      } else if (inputAllowed) {
        requestTextInput(id);
        textInput = true;
      }
    }

    if (textInput) {
      // Clamp typed input only when ImGuiSliderFlags_ClampOnInput is set, like ImGui::DragScalar.
      bool clamp = false;
      if ((flags & ImGuiSliderFlags_ClampOnInput) && (min || max)) {
        const int rangeDirection = (min && max) ? ImGui::DataTypeCompare(dataType, min, max) : 0;
        if (!min || !max || rangeDirection < 0) {
          clamp = true;
        } else if (rangeDirection == 0) {
          clamp = ImGui::DataTypeIsZero(dataType, min) ? (flags & ImGuiSliderFlags_ClampZeroRange) != 0 : true;
        }
      }
      return textInputNumber(frame, id, idLabel, dataType, data, format, clamp ? min : nullptr, clamp ? max : nullptr);
    }

    // Draw the block. The arrow zones appear only on hover.
    auto* drawList = ImGui::GetWindowDrawList();
    const bool active = g.ActiveId == id;
    ImGui::RenderNavCursor(frame, id);
    drawBlock(drawList, frame.Min, frame.Max, BlockKind::Raised, hovered, active, corners);
    if (hovered || active) {
      // Each zone keeps only the outer corners of the block.
      const ImU32 zone = ImGui::GetColorU32(blockStyle.arrowZone);
      const ImDrawFlags all = (corners & ImDrawFlags_RoundCornersMask_) ? corners : ImDrawFlags_RoundCornersAll;
      const ImDrawFlags leftCorners = all & ImDrawFlags_RoundCornersLeft;
      const ImDrawFlags rightCorners = all & ImDrawFlags_RoundCornersRight;
      drawList->AddRectFilled(leftArrow.Min, leftArrow.Max, zone, blockStyle.rounding, leftCorners ? leftCorners : ImDrawFlags_RoundCornersNone);
      drawList->AddRectFilled(rightArrow.Min, rightArrow.Max, zone, blockStyle.rounding, rightCorners ? rightCorners : ImDrawFlags_RoundCornersNone);

      const float arrowScale = 0.6f;
      const float arrowSize = g.FontSize * arrowScale;
      const float arrowY = frame.GetCenter().y - arrowSize * 0.5f;
      const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
      ImGui::RenderArrow(drawList, ImVec2(leftArrow.GetCenter().x - arrowSize * 0.5f, arrowY), text, ImGuiDir_Left, arrowScale);
      ImGui::RenderArrow(drawList, ImVec2(rightArrow.GetCenter().x - arrowSize * 0.5f, arrowY), text, ImGuiDir_Right, arrowScale);
    }

    const bool dragChanged = ImGui::DragBehavior(id, dataType, data, speed, min, max, format, flags);
    const bool changed = stepped || dragChanged;
    if (changed) {
      ImGui::MarkItemEdited(id);
    }

    drawNumberText(frame, blockStyle.arrowWidth + blockStyle.textPadding * 0.5f, label, dataType, data, format);
    return changed;
  }



  //=========================
  // Slider and progress blocks
  //=========================

  // Read a number of any ImGui data type as a double.
  static double toDouble(ImGuiDataType dataType, const void* data) {
    switch (dataType) {
      case ImGuiDataType_S8:
        return *(const ImS8*)data;
      case ImGuiDataType_U8:
        return *(const ImU8*)data;
      case ImGuiDataType_S16:
        return *(const ImS16*)data;
      case ImGuiDataType_U16:
        return *(const ImU16*)data;
      case ImGuiDataType_S32:
        return *(const ImS32*)data;
      case ImGuiDataType_U32:
        return *(const ImU32*)data;
      case ImGuiDataType_S64:
        return (double)*(const ImS64*)data;
      case ImGuiDataType_U64:
        return (double)*(const ImU64*)data;
      case ImGuiDataType_Float:
        return *(const float*)data;
      case ImGuiDataType_Double:
        return *(const double*)data;
      default:
        IM_ASSERT(false && "Unexpected data type");
        return 0.0;
    }
  }


  // Draw a fill from the left edge of [min, max] to `fraction` of its width.
  // The fill keeps the left corners of the block, and the right corners when it is full.
  static void drawFill(ImDrawList* drawList, ImVec2 min, ImVec2 max, float fraction, ImU32 color, ImDrawFlags corners) {
    if (fraction <= 0.0f) {
      return;
    }

    const auto& style = getBlockStyle();
    const ImDrawFlags all = (corners & ImDrawFlags_RoundCornersMask_) ? corners : ImDrawFlags_RoundCornersAll;
    const ImDrawFlags fillCorners = fraction >= 1.0f ? all : all & ImDrawFlags_RoundCornersLeft;
    const float right = min.x + (max.x - min.x) * std::min(fraction, 1.0f);
    drawList->AddRectFilled(min, ImVec2(right, max.y), color, style.rounding, fillCorners ? fillCorners : ImDrawFlags_RoundCornersNone);
  }


  bool sliderBlock(const char* label, ImGuiDataType dataType, void* data, const void* min, const void* max, const char* format,
                   ImGuiSliderFlags flags, ImDrawFlags corners) {
    const bool inputAllowed = (flags & ImGuiSliderFlags_NoInput) == 0;
    char idLabel[128];
    makeIdLabel(idLabel, label);
    ImGuiID id;
    ImRect frame;
    if (!addNumberItem(idLabel, inputAllowed, id, frame)) {
      return false;
    }

    ImGuiContext& g = *GImGui;
    const auto& blockStyle = getBlockStyle();
    if (format == nullptr) {
      format = ImGui::DataTypeGetInfo(dataType)->PrintFmt;
    }

    bool hovered;
    bool textInput = activateNumber(id, frame, dataType, data, inputAllowed, hovered);

    // A release without a drag opens text input. SliderBehavior() clears the active ID on release,
    // so this check must come first.
    if (!textInput && hovered && inputAllowed && isReleasedWithoutDrag(id)) {
      requestTextInput(id);
      textInput = true;
    }

    if (textInput) {
      // Only clamp typed input when ImGuiSliderFlags_ClampOnInput is set, like ImGui::SliderScalar.
      const bool clamp = (flags & ImGuiSliderFlags_ClampOnInput) != 0;
      return textInputNumber(frame, id, idLabel, dataType, data, format, clamp ? min : nullptr, clamp ? max : nullptr);
    }

    // A mouse press must not move the value, because a release without a drag opens text input.
    // Keyboard and gamepad input go to SliderBehavior() at once.
    const bool active = g.ActiveId == id;
    bool changed = false;
    if (active && g.ActiveIdSource == ImGuiInputSource_Mouse && !isDragging()) {
      if (!g.IO.MouseDown[0]) {
        // The mouse left the block before release, so neither a drag nor text input happens.
        ImGui::ClearActiveID();
      }
    } else {
      // The grab is not drawn. With no minimum size, the grab position spans the full block.
      ImRect grab;
      ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 0.0f);
      changed = ImGui::SliderBehavior(frame, id, dataType, data, min, max, format, flags, &grab);
      ImGui::PopStyleVar();
      if (changed) {
        ImGui::MarkItemEdited(id);
      }
    }

    // Draw the block and the fill to the current value.
    // TODO: The fill is linear, so it does not match ImGuiSliderFlags_Logarithmic.
    auto* drawList = ImGui::GetWindowDrawList();
    const double low = toDouble(dataType, min);
    const double range = toDouble(dataType, max) - low;
    const float fraction = range != 0.0 ? (float)std::clamp((toDouble(dataType, data) - low) / range, 0.0, 1.0) : 0.0f;

    ImGui::RenderNavCursor(frame, id);
    drawBlock(drawList, frame.Min, frame.Max, BlockKind::Raised, hovered, active, corners);
    drawFill(drawList, frame.Min, frame.Max, fraction, ImGui::GetColorU32(blockStyle.fill), corners);

    drawNumberText(frame, blockStyle.textPadding, label, dataType, data, format);
    return changed;
  }


  void progressBlock(float fraction, const char* text, ImVec2 size) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) {
      return;
    }

    const auto& blockStyle = getBlockStyle();
    const ImVec2 itemSize = ImGui::CalcItemSize(size, ImGui::CalcItemWidth(), ImGui::GetFrameHeight());
    const ImVec2 position = window->DC.CursorPos;
    const ImRect bounds(position, ImVec2(position.x + itemSize.x, position.y + itemSize.y));
    ImGui::ItemSize(itemSize, ImGui::GetStyle().FramePadding.y);
    if (!ImGui::ItemAdd(bounds, 0)) {
      return;
    }

    // Unlike the slider fill, the progress fill keeps all four round corners.
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    drawBlock(window->DrawList, bounds.Min, bounds.Max, BlockKind::Lowered, false, false);
    if (fraction > 0.0f) {
      const float right = bounds.Min.x + bounds.GetWidth() * fraction;
      window->DrawList->AddRectFilled(bounds.Min, ImVec2(right, bounds.Max.y), ImGui::GetColorU32(blockStyle.fill), blockStyle.rounding);
    }

    char percent[16];
    if (text == nullptr) {
      snprintf(percent, sizeof(percent), "%.0f%%", fraction * 100.0f);
      text = percent;
    }
    ImGui::RenderTextClipped(bounds.Min, bounds.Max, text, nullptr, nullptr, ImVec2(0.5f, 0.5f));
  }

}  // namespace ren::eui
