#include <ren/core/ui/Widgets.h>
#include <ren/core/ui/EditorUI.h>

#include <imgui_internal.h>
#include <cstdarg>

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

}  // namespace ren::eui
