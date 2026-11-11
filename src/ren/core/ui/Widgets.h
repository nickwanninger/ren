#pragma once

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace ren::eui {

  // Maps an arithmetic C++ type to the ImGui data type with the same width.
  template <typename T>
  constexpr ImGuiDataType dataTypeOf() {
    static_assert(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>, "Expected a number type");

    // Float/double are special, integers need to get mapped by size.
    if constexpr (std::is_same_v<T, float>) {
      return ImGuiDataType_Float;
    } else if constexpr (std::is_same_v<T, double>) {
      return ImGuiDataType_Double;
    } else if constexpr (sizeof(T) == 1) {
      // 1 byte integers
      return std::is_signed_v<T> ? ImGuiDataType_S8 : ImGuiDataType_U8;
    } else if constexpr (sizeof(T) == 2) {
      // 2 byte integers
      return std::is_signed_v<T> ? ImGuiDataType_S16 : ImGuiDataType_U16;
    } else if constexpr (sizeof(T) == 4) {
      // 4 byte integers
      return std::is_signed_v<T> ? ImGuiDataType_S32 : ImGuiDataType_U32;
    } else {
      // 8 byte integers
      return std::is_signed_v<T> ? ImGuiDataType_S64 : ImGuiDataType_U64;
    }
  }


  // Describes the types that drag() and slide() accept: a number, or a glm
  // vector of numbers. `Component` is the number type of one field.
  template <typename T>
  struct NumberTraits {
    static constexpr bool isNumber = std::is_arithmetic_v<T> && !std::is_same_v<T, bool>;
    static constexpr bool isVector = false;
    using Component = T;
  };

  template <int N, typename T>
  struct NumberTraits<glm::vec<N, T, glm::defaultp>> {
    static constexpr bool isNumber = false;
    static constexpr bool isVector = true;
    using Component = T;
  };


  // Bounds use the component type. Step is drag speed, not a rounding interval.
  template <typename T = float>
  struct DragOptions {
    std::optional<T> min;
    std::optional<T> max;
    float step = 0.1f;
    const char* format = nullptr;
    ImGuiSliderFlags flags = ImGuiSliderFlags_None;
  };


  // Bounds apply to every component of a slider.
  template <typename T = float>
  struct SlideOptions {
    T min = 0;
    T max = 1;
    const char* format = nullptr;
    ImGuiSliderFlags flags = ImGuiSliderFlags_None;
  };


  // Controls text input in Inspect().
  struct TextOptions {
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_None;
  };


  //=========================
  // Blocks
  //=========================

  // Visual depth of an editor block. Raised blocks are controls that you press
  // or drag. Lowered blocks hold text that you type.
  enum class BlockKind {
    Raised,
    Lowered,
  };


  // Colors and sizes for editor blocks. Each color is an ImGui style slot, so blocks follow the
  // current theme (see applyTheme()).
  struct BlockStyle {
    ImGuiCol raised = ImGuiCol_Button;
    ImGuiCol raisedHovered = ImGuiCol_ButtonHovered;
    ImGuiCol raisedActive = ImGuiCol_ButtonActive;
    // The step arrow zones of a hovered drag block. They are darker than the hovered block.
    ImGuiCol arrowZone = ImGuiCol_Button;
    ImGuiCol lowered = ImGuiCol_FrameBg;
    ImGuiCol loweredHovered = ImGuiCol_FrameBgHovered;
    ImGuiCol loweredActive = ImGuiCol_FrameBgActive;
    // A dark line under each block that makes it look like it sits above the panel.
    ImGuiCol backdrop = ImGuiCol_Border;
    // The slider and progress fill that shows the value.
    ImGuiCol fill = ImGuiCol_SliderGrabActive;

    float rounding = 4.0f;
    // Distance from the bottom edge of a block to the center of its backdrop line.
    float backdropOffset = 1.5f;
    // Space between blocks that join into one group, such as vector rows.
    float joinSpacing = 2.0f;
    // Width of the step arrow zone at each end of a drag block.
    float arrowWidth = 13.0f;
    // Space between a block edge and its text.
    float textPadding = 8.0f;
  };


  // Return the block style that all block widgets use.
  const BlockStyle& getBlockStyle();


  // Draw a block background in [min, max] without adding an item. `corners`
  // takes ImDrawFlags_RoundCorners* flags, so blocks in a group can join.
  void drawBlock(ImDrawList* drawList, ImVec2 min, ImVec2 max, BlockKind kind, bool hovered, bool active,
                 ImDrawFlags corners = ImDrawFlags_RoundCornersAll);


  // Controls one block() item.
  struct BlockOptions {
    BlockKind kind = BlockKind::Raised;
    // Zero uses the item width and frame height, like ImGui::Button.
    ImVec2 size = ImVec2(0, 0);
    // Horizontal text position: 0 is left, 0.5 is center, 1 is right.
    float textAlign = 0.5f;
    ImGuiCol textColor = ImGuiCol_Text;
    ImDrawFlags corners = ImDrawFlags_RoundCornersAll;
  };


  // Draw a block as one ImGui item, with the text of `label` before any "##".
  // Return true when the user clicks it.
  bool block(const char* label, const BlockOptions& options = {});


  // Draw a raised drag field with the label on the left and the value on the right.
  // A click on an end arrow steps the value. Ctrl+click or double click types a value.
  // This follows ImGui::DragScalar, and takes the same arguments.
  bool dragBlock(const char* label, ImGuiDataType dataType, void* data, float speed, const void* min = nullptr, const void* max = nullptr,
                 const char* format = nullptr, ImGuiSliderFlags flags = ImGuiSliderFlags_None, ImDrawFlags corners = ImDrawFlags_RoundCornersAll);


  // Draw a raised slider. A fill from the left edge shows the value, with the label on the
  // left and the value on the right. A click sets the value; Ctrl+click types a value.
  // This follows ImGui::SliderScalar, and takes the same arguments.
  bool sliderBlock(const char* label, ImGuiDataType dataType, void* data, const void* min, const void* max, const char* format = nullptr,
                   ImGuiSliderFlags flags = ImGuiSliderFlags_None, ImDrawFlags corners = ImDrawFlags_RoundCornersAll);


  // Draw a lowered progress bar for `fraction` in [0, 1], with `text` in the center. Null text shows a percentage.
  // Zero size uses the item width and frame height.
  void progressBlock(float fraction, const char* text = nullptr, ImVec2 size = ImVec2(0, 0));


  namespace detail {
    // Draw `count` joined blocks side by side in the item width. `drawBlock(i, corners)` draws
    // block i with the item width set. The group makes the blocks act as one item.
    template <typename DrawBlock>
    bool blockRow(const char* id, int count, DrawBlock&& drawBlock) {
      // SetNextItemWidth() applies only to the next item, so split the width once for all blocks.
      const float spacing = getBlockStyle().joinSpacing;
      const float width = std::max(1.0f, (ImGui::CalcItemWidth() - spacing * (float)(count - 1)) / (float)count);

      bool changed = false;
      ImGui::PushID(id);
      ImGui::BeginGroup();
      for (int i = 0; i < count; ++i) {
        // Only the outer corners of the group are round.
        ImDrawFlags corners = ImDrawFlags_RoundCornersNone;
        if (count == 1) {
          corners = ImDrawFlags_RoundCornersAll;
        } else if (i == 0) {
          corners = ImDrawFlags_RoundCornersLeft;
        } else if (i == count - 1) {
          corners = ImDrawFlags_RoundCornersRight;
        }

        if (i != 0) {
          ImGui::SameLine(0.0f, spacing);
        }
        ImGui::PushID(i);
        ImGui::SetNextItemWidth(width);
        changed |= drawBlock(i, corners);
        ImGui::PopID();
      }
      ImGui::EndGroup();
      ImGui::PopID();

      return changed;
    }


    inline const char* componentLabels[] = {"X", "Y", "Z", "W"};
  }  // namespace detail


  // Draw a drag block for a number, or one joined block per vector component (X, Y, Z, W)
  // side by side in the item width. `id` identifies the field and is not shown.
  template <typename T>
    requires(NumberTraits<T>::isNumber || NumberTraits<T>::isVector)
  bool dragBlock(const char* id, T& value, const DragOptions<typename NumberTraits<T>::Component>& options = {}) {
    using Component = typename NumberTraits<T>::Component;
    const auto* min = options.min ? &*options.min : nullptr;
    const auto* max = options.max ? &*options.max : nullptr;

    if constexpr (NumberTraits<T>::isNumber) {
      return dragBlock(id, dataTypeOf<T>(), &value, options.step, min, max, options.format, options.flags);
    } else {
      return detail::blockRow(id, T::length(), [&](int i, ImDrawFlags corners) {
        return dragBlock(detail::componentLabels[i], dataTypeOf<Component>(), &value[i], options.step, min, max, options.format, options.flags,
                         corners);
      });
    }
  }


  // Draw a slider block for a number, or one joined slider per vector component (X, Y, Z, W)
  // side by side in the item width. All components share the bounds.
  template <typename T>
    requires(NumberTraits<T>::isNumber || NumberTraits<T>::isVector)
  bool sliderBlock(const char* id, T& value, const SlideOptions<typename NumberTraits<T>::Component>& options) {
    using Component = typename NumberTraits<T>::Component;

    if constexpr (NumberTraits<T>::isNumber) {
      return sliderBlock(id, dataTypeOf<T>(), &value, &options.min, &options.max, options.format, options.flags);
    } else {
      return detail::blockRow(id, T::length(), [&](int i, ImDrawFlags corners) {
        return sliderBlock(detail::componentLabels[i], dataTypeOf<Component>(), &value[i], &options.min, &options.max, options.format, options.flags,
                           corners);
      });
    }
  }


  namespace detail {
    // Restores an ImGui scope on return or exception.
    template <typename Finish>
    class ScopeExit {
     public:
      explicit ScopeExit(Finish finish)
          : finish(std::move(finish)) {}
      ~ScopeExit() { finish(); }
      ScopeExit(const ScopeExit&) = delete;
      ScopeExit& operator=(const ScopeExit&) = delete;

     private:
      Finish finish;
    };


    // Draw the switch for the last submitted item. Height controls its visual size.
    inline void drawToggle(ImVec2 position, float height, bool value) {
      const float width = height * 1.8f;

      // Keep the knob position per widget, and move it toward the current value.
      float& knob = *ImGui::GetStateStorage()->GetFloatRef(ImGui::GetItemID(), value ? 1.0f : 0.0f);
      const float target = value ? 1.0f : 0.0f;
      const float step = ImGui::GetIO().DeltaTime * 12.0f;
      knob = knob < target ? std::min(target, knob + step) : std::max(target, knob - step);

      // The track fades from the frame color (off) to the accent color (on).
      const ImVec4 off = ImGui::GetStyleColorVec4(ImGui::IsItemHovered() ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
      const ImVec4 on = ImGui::GetStyleColorVec4(ImGuiCol_SliderGrabActive);
      const ImVec4 track(off.x + (on.x - off.x) * knob, off.y + (on.y - off.y) * knob, off.z + (on.z - off.z) * knob, off.w + (on.w - off.w) * knob);

      auto* drawList = ImGui::GetWindowDrawList();
      const float radius = height * 0.5f;
      drawList->AddRectFilled(position, ImVec2(position.x + width, position.y + height), ImGui::GetColorU32(track), radius);
      const float knobX = position.x + radius + knob * (width - 2.0f * radius);
      drawList->AddCircleFilled(ImVec2(knobX, position.y + radius), radius - 2.0f, ImGui::GetColorU32(ImGuiCol_CheckMark));
    }


    // Draw a switch with its optional label in normal ImGui layout.
    inline bool toggleValue(const char* id, bool& value) {
      const auto& style = ImGui::GetStyle();
      const float height = ImGui::GetFrameHeight();
      const float width = height * 1.8f;

      auto id_view = std::string_view(id);
      const auto label = id_view.substr(0, id_view.find("##"));
      const float labelWidth = label.empty() ? 0.0f : style.ItemInnerSpacing.x + ImGui::CalcTextSize(label.data(), label.data() + label.size()).x;

      // One button covers the switch and its label, like ImGui::Checkbox.
      const ImVec2 position = ImGui::GetCursorScreenPos();
      const bool changed = ImGui::InvisibleButton(id, ImVec2(width + labelWidth, height), ImGuiButtonFlags_EnableNav);
      if (changed) {
        value = !value;
      }

      drawToggle(position, height, value);

      if (!label.empty()) {
        const ImVec2 textPosition(position.x + width + style.ItemInnerSpacing.x, position.y + style.FramePadding.y);
        ImGui::GetWindowDrawList()->AddText(textPosition, ImGui::GetColorU32(ImGuiCol_Text), label.data(), label.data() + label.size());
      }

      return changed;
    }
  }  // namespace detail


  // A borrowed UI handle for one callback. Pass it by reference to inspectors.
  // Groups share their parent's layout; content callbacks use normal ImGui layout.
  // Handles cannot be copied or moved, and must not escape their callback.
  class Ui {
   public:
    Ui(const Ui&) = delete;
    Ui& operator=(const Ui&) = delete;

    // Scopes

    // Draw a table of aligned value rows. Return false when the table is hidden.
    template <typename Draw>
    bool valueTable(const char* id, Draw&& draw) {
      return content([&](Ui&) {
        if (!beginTable(id)) {
          return false;
        }
        detail::ScopeExit finish([] { endTable(); });
        Ui child(true);
        std::forward<Draw>(draw)(child);
        return true;
      });
    }

    // Draw an expanded group. Its header controls collapse; the optional switch controls enable state.
    template <typename Draw>
    bool group(const char* label, bool* enabled, Draw&& draw) {
      if (!beginGroup(label, enabled)) {
        return false;
      }
      detail::ScopeExit finish([&] { endGroup(); });
      std::forward<Draw>(draw)(*this);
      return true;
    }

    // Draw a group without an enable switch. Return true when its callback runs.
    template <typename Draw>
    bool group(const char* label, Draw&& draw) {
      return group(label, nullptr, std::forward<Draw>(draw));
    }

    // Draw a full-width freeform block. Preserve indentation and disabled state.
    template <typename Draw>
    decltype(auto) content(Draw&& draw) {
      validate();
      if (!values) {
        return std::forward<Draw>(draw)(*this);
      }

      float maxX, itemWidth;
      beginContent(maxX, itemWidth);
      detail::ScopeExit finish([&] { endContent(maxX, itemWidth); });
      Ui child(false);
      return std::forward<Draw>(draw)(child);
    }

    // Run existing ImGui code inside a freeform block. The callback takes no arguments.
    template <typename Draw>
    decltype(auto) imgui(Draw&& draw) {
      return content([&](Ui&) -> decltype(auto) { return std::forward<Draw>(draw)(); });
    }

    // Value fields

    // Draw a drag block. A vector gets one joined row per component. Step controls drag speed.
    template <typename T>
    bool drag(const char* label, T& value, const DragOptions<typename NumberTraits<T>::Component>& options = {}) {
      static_assert(NumberTraits<T>::isNumber || NumberTraits<T>::isVector);
      return field(label, [&](const char* id) { return dragBlock(id, value, options); });
    }

    // Draw a slider with shared bounds for all vector components.
    template <typename T>
    bool slide(const char* label, T& value, const SlideOptions<typename NumberTraits<T>::Component>& options) {
      static_assert(NumberTraits<T>::isNumber || NumberTraits<T>::isVector);
      return field(label, [&](const char* id) { return sliderBlock(id, value, options); });
    }

    // Draw an animated switch. Return true when the value changes.
    bool toggle(const char* label, bool& value) {
      return field(label, [&](const char* id) { return detail::toggleValue(id, value); });
    }

    // Default editors

    // Draw the default bool editor.
    bool inspect(const char* label, bool& value) { return toggle(label, value); }

    // Draw the default text editor.
    bool inspect(const char* label, std::string& value, const TextOptions& options = {}) {
      return field(label, [&](const char* id) { return ImGui::InputText(id, &value, options.flags); });
    }

    // Draw the default editor for a number or vector.
    template <typename T>
      requires(NumberTraits<T>::isNumber || NumberTraits<T>::isVector)
    bool inspect(const char* label, T& value, const DragOptions<typename NumberTraits<T>::Component>& options = {}) {
      return drag(label, value, options);
    }

    // Edit the four stored components of a quaternion.
    bool inspect(const char* label, glm::quat& value, const DragOptions<float>& options = {}) {
      auto components = glm::vec4(value.x, value.y, value.z, value.w);
      if (!drag(label, components, options)) {
        return false;
      }
      value = glm::quat(components.w, components.x, components.y, components.z);
      return true;
    }

    // Display

    // Draw printf-style text in a full-width row.
    void text(const char* fmt, ...) IM_FMTARGS(2);

    // Draw printf-style text with the editor's monospace font.
    void textMono(const char* fmt, ...) IM_FMTARGS(2);

    // Draw unformatted text in a full-width row.
    void textUnformatted(const char* text);

    // Draw a separator, with an optional title, across the available width.
    void separator(const char* label = nullptr);

    // Draw an image in a full-width row.
    void image(ImTextureRef texture, ImVec2 size, ImVec2 uv0 = ImVec2(0, 0), ImVec2 uv1 = ImVec2(1, 1));

    // Draw an editor button with an optional icon.
    bool button(const char* label, const char* icon = nullptr, ImVec2 size = ImVec2(0, 0));

    // Continue the previous line in a content callback.
    void sameLine(float offset = 0.0f, float spacing = -1.0f);

    // Containers

    // Draw a child window with its own UI handle. End it even when hidden or when draw() throws.
    template <typename Draw>
    bool child(const char* id, Draw&& draw, ImVec2 size = ImVec2(0, 0), ImGuiChildFlags flags = 0, ImGuiWindowFlags windowFlags = 0) {
      return content([&](Ui&) {
        const bool visible = ImGui::BeginChild(id, size, flags, windowFlags);
        detail::ScopeExit finish([] { ImGui::EndChild(); });
        if (!visible) {
          return false;
        }
        Ui child(false);
        std::forward<Draw>(draw)(child);
        return true;
      });
    }

    // Draw a tab bar. Return false when its callback does not run.
    template <typename Draw>
    bool tabBar(const char* id, Draw&& draw, ImGuiTabBarFlags flags = 0) {
      return content([&](Ui& ui) {
        if (!beginTabBar(id, flags)) {
          return false;
        }
        detail::ScopeExit finish([] { ImGui::EndTabBar(); });
        std::forward<Draw>(draw)(ui);
        return true;
      });
    }

    // Draw an active tab. Return false when another tab is active.
    template <typename Draw>
    bool tab(const char* label, Draw&& draw) {
      return content([&](Ui& ui) {
        if (!ImGui::BeginTabItem(label)) {
          return false;
        }
        detail::ScopeExit finish([] { ImGui::EndTabItem(); });
        std::forward<Draw>(draw)(ui);
        return true;
      });
    }

   private:
    template <typename Draw>
    friend decltype(auto) draw(Draw&& draw);

    explicit Ui(bool values);
    void validate() const;

    static bool beginTable(const char* id);
    static void endTable();
    bool beginGroup(const char* label, bool* enabled);
    void endGroup();
    void beginContent(float& maxX, float& itemWidth);
    void endContent(float maxX, float itemWidth);
    void beginField(const char* label);
    static bool beginTabBar(const char* id, ImGuiTabBarFlags flags);

    template <typename Draw>
    bool field(const char* label, Draw&& draw) {
      validate();
      if (!values) {
        return std::forward<Draw>(draw)(label);
      }

      beginField(label);
      const auto id = std::string("##") + label;
      return std::forward<Draw>(draw)(id.c_str());
    }

    ImGuiContext* context;
    void* window;
    ImGuiID tableId;
    bool values;
  };


  // Create a freeform UI handle for the callback in the current ImGui window.
  template <typename Draw>
  decltype(auto) draw(Draw&& draw) {
    Ui ui(false);
    return std::forward<Draw>(draw)(ui);
  }


  // Draw a root value table in the current ImGui window.
  template <typename Draw>
  bool valueTable(const char* id, Draw&& draw) {
    return eui::draw([&](Ui& ui) { return ui.valueTable(id, std::forward<Draw>(draw)); });
  }

}  // namespace ren::eui
