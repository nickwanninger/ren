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


  // Draws a drag field for one number. Ctrl+click types a value.
  template <typename T>
  bool DragNumber(const char* id, T& value, float speed = 0.1f) {
    return ImGui::DragScalar(id, dataTypeOf<T>(), &value, speed);
  }


  // Draws one labeled drag field per component (X, Y, Z, W) in the current
  // item width. The components act as one control inside a group. Like ImGui
  // widgets, it shows the text of `id` before any "##" after the fields.
  template <int N, typename T>
  bool DragVector(const char* id, glm::vec<N, T, glm::defaultp>& value, float speed = 0.1f) {
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
      changed |= DragNumber("##component", value[i], speed);
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


  template <typename T>
  struct IsVector : std::false_type {};

  template <int N, typename T>
  struct IsVector<glm::vec<N, T, glm::defaultp>> : std::true_type {};


  // Draws the default widget for T: a checkbox, a text field, a number field, or
  // a vector of number fields. A quaternion shows its four stored components.
  template <typename T>
  bool Inspect(const char* id, T& value) {
    if constexpr (std::is_same_v<T, bool>) {
      return ImGui::Checkbox(id, &value);
    } else if constexpr (std::is_same_v<T, std::string>) {
      return ImGui::InputText(id, &value);
    } else if constexpr (std::is_arithmetic_v<T>) {
      return DragNumber(id, value);
    } else if constexpr (IsVector<T>::value) {
      return DragVector(id, value);
    } else if constexpr (std::is_same_v<T, glm::quat>) {
      auto components = glm::vec4(value.x, value.y, value.z, value.w);
      if (!DragVector(id, components, 0.01f)) {
        return false;
      }
      value = glm::quat(components.w, components.x, components.y, components.z);
      return true;
    } else {
      static_assert(sizeof(T) == 0, "eui::Inspect has no widget for this type");
    }
  }

}  // namespace ren::eui
