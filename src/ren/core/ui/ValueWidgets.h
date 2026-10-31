#pragma once

// Value widgets for editors. Each widget draws one control for a value and
// returns true when it changed the value. They do not know about undo: wrap
// them in edit::editValue() or EditContext::edit() to record their changes.

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cstdint>
#include <string_view>
#include <string>
#include <type_traits>

namespace ren::eui {

  // Maps an arithmetic C++ type to the ImGui data type with the same width.
  template <typename T>
  constexpr ImGuiDataType dataTypeOf() {
    static_assert(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>, "Expected a number type");
    if constexpr (std::is_same_v<T, float>) {
      return ImGuiDataType_Float;
    } else if constexpr (std::is_same_v<T, double>) {
      return ImGuiDataType_Double;
    } else if constexpr (sizeof(T) == 1) {
      return std::is_signed_v<T> ? ImGuiDataType_S8 : ImGuiDataType_U8;
    } else if constexpr (sizeof(T) == 2) {
      return std::is_signed_v<T> ? ImGuiDataType_S16 : ImGuiDataType_U16;
    } else if constexpr (sizeof(T) == 4) {
      return std::is_signed_v<T> ? ImGuiDataType_S32 : ImGuiDataType_U32;
    } else {
      return std::is_signed_v<T> ? ImGuiDataType_S64 : ImGuiDataType_U64;
    }
  }


  // Describes the types that Drag() and Slide() accept: a number, or a glm
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


  namespace detail {
    // Draws one labeled field per component (X, Y, Z, W) in the current item
    // width, with `drawComponent` for each field. The components act as one
    // control inside a group. Like ImGui widgets, it shows the text of `id`
    // before any "##" after the fields.
    template <int N, typename T, typename DrawComponent>
    bool vectorRow(const char* id, glm::vec<N, T, glm::defaultp>& value, DrawComponent&& drawComponent) {
      static_assert(N >= 1 && N <= 4);
      static const char* labels[] = {"X", "Y", "Z", "W"};

      const auto& style = ImGui::GetStyle();
      const float groupWidth = (ImGui::CalcItemWidth() - ((float)N - 1.0f) * style.ItemSpacing.x) / (float)N;

      bool changed = false;
      ImGui::PushID(id);
      ImGui::BeginGroup();
      for (int i = 0; i < N; ++i) {
        if (i != 0) {
          ImGui::SameLine();
        }
        ImGui::PushID(i);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(labels[i]);
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        const float width = groupWidth - ImGui::CalcTextSize(labels[i]).x - style.ItemInnerSpacing.x;
        ImGui::SetNextItemWidth(std::max(1.0f, width));
        changed |= drawComponent(value[i]);
        ImGui::PopID();
      }
      const auto label = std::string_view(id).substr(0, std::string_view(id).find("##"));
      if (!label.empty()) {
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label.data(), label.data() + label.size());
      }
      ImGui::EndGroup();
      ImGui::PopID();
      return changed;
    }
  }  // namespace detail


  // Draws a drag field for a number, or one drag field per component of a
  // vector as one control. Ctrl+click types a value.
  template <typename T>
  bool Drag(const char* id, T& value, float speed = 0.1f) {
    using Traits = NumberTraits<T>;
    static_assert(Traits::isNumber || Traits::isVector, "eui::Drag expects a number or a glm vector");

    if constexpr (Traits::isNumber) {
      return ImGui::DragScalar(id, dataTypeOf<T>(), &value, speed);
    } else {
      return detail::vectorRow(id, value, [speed](auto& component) { return Drag("##component", component, speed); });
    }
  }


  // Draws a slider for a number, or one slider per component of a vector as
  // one control. Every component has the same bounds. Ctrl+click types a
  // value; ImGui clamps it to the bounds.
  template <typename T>
  bool Slide(const char* id, T& value, typename NumberTraits<T>::Component min, typename NumberTraits<T>::Component max) {
    using Traits = NumberTraits<T>;
    static_assert(Traits::isNumber || Traits::isVector, "eui::Slide expects a number or a glm vector");

    if constexpr (Traits::isNumber) {
      return ImGui::SliderScalar(id, dataTypeOf<T>(), &value, &min, &max);
    } else {
      return detail::vectorRow(id, value, [min, max](auto& component) { return Slide("##component", component, min, max); });
    }
  }


  // Draws an on/off switch for a bool, with the text of `id` before any "##"
  // after it. A click on the switch or its label flips the value. The knob
  // slides to its new side over a few frames.
  inline bool Toggle(const char* id, bool& value) {
    const auto& style = ImGui::GetStyle();
    const float height = ImGui::GetFrameHeight();
    const float width = height * 1.8f;
    const auto label = std::string_view(id).substr(0, std::string_view(id).find("##"));
    const float labelWidth = label.empty() ? 0.0f : style.ItemInnerSpacing.x + ImGui::CalcTextSize(label.data(), label.data() + label.size()).x;

    // One button covers the switch and its label, like ImGui::Checkbox.
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const bool changed = ImGui::InvisibleButton(id, ImVec2(width + labelWidth, height), ImGuiButtonFlags_EnableNav);
    if (changed) {
      value = !value;
    }

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

    if (!label.empty()) {
      const ImVec2 textPosition(position.x + width + style.ItemInnerSpacing.x, position.y + style.FramePadding.y);
      drawList->AddText(textPosition, ImGui::GetColorU32(ImGuiCol_Text), label.data(), label.data() + label.size());
    }
    return changed;
  }


  // Draws the default widget for T: a toggle, a text field, or a drag field
  // for a number or vector. A quaternion shows its four stored components.
  template <typename T, typename... Args>
  bool Inspect(const char* id, T& value, Args&&... args) {
    if constexpr (std::is_same_v<T, bool>) {
      return Toggle(id, value, std::forward<Args>(args)...);
    } else if constexpr (std::is_same_v<T, std::string>) {
      return ImGui::InputText(id, &value, std::forward<Args>(args)...);
    } else if constexpr (NumberTraits<T>::isNumber || NumberTraits<T>::isVector) {
      return Drag(id, value, std::forward<Args>(args)...);
    } else if constexpr (std::is_same_v<T, glm::quat>) {
      auto components = glm::vec4(value.x, value.y, value.z, value.w);
      if (!Drag(id, components, std::forward<Args>(args)...)) {
        return false;
      }
      value = glm::quat(components.w, components.x, components.y, components.z);
      return true;
    } else {
      static_assert(sizeof(T) == 0, "eui::Inspect has no widget for this type");
    }
  }

}  // namespace ren::eui
