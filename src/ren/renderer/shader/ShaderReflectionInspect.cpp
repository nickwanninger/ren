#include "./ShaderReflection.h"

#include <imgui/imgui.h>

namespace ren {
  namespace {

    constexpr ImGuiTreeNodeFlags treeNodeFlagsBase =
        ImGuiTreeNodeFlags_SpanAllColumns |
        ImGuiTreeNodeFlags_DefaultOpen |
        ImGuiTreeNodeFlags_DrawLinesFull;

    void inspectNodeRecursive(const ShaderReflection::Node* node) {
      if (node == nullptr) {
        return;
      }

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);

      const bool isLeaf = node->members.empty();
      auto nodeFlags = treeNodeFlagsBase;
      if (isLeaf) {
        nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
      }

      char name[256];
      if (node->name.empty()) {
        if (node->location.arrayIndex) {
          if (*node->location.arrayIndex > 0) {
            nodeFlags &= ~ImGuiTreeNodeFlags_DefaultOpen;
          }
          snprintf(name, sizeof(name), "[#%d]", *node->location.arrayIndex);
        } else {
          snprintf(name, sizeof(name), "<unnamed>");
        }
      } else {
        snprintf(name, sizeof(name), "%s", node->name.c_str());
      }

      const bool open = ImGui::TreeNodeEx(
          const_cast<ShaderReflection::Node*>(node), nodeFlags, "%s", name);

      ImGui::TableNextColumn();
      ImGui::Text("%s", node->type.toString().c_str());
      ImGui::TableNextColumn();
      ImGui::Text("%s", node->location.pushConstant ? "Yes" : "");

#define SHOW_LOCATION(field)                      \
  ImGui::TableNextColumn();                       \
  if (node->location.field) {                     \
    ImGui::Text("%d", *node->location.field);     \
  }

      SHOW_LOCATION(bindingSet);
      SHOW_LOCATION(bindingIndex);
      SHOW_LOCATION(byteOffset);
      SHOW_LOCATION(byteSize);
      SHOW_LOCATION(arrayIndex);
      SHOW_LOCATION(varyingIn);
      SHOW_LOCATION(varyingOut);

#undef SHOW_LOCATION

      if (open && !isLeaf) {
        for (const auto* member : node->members) {
          inspectNodeRecursive(member);
        }
        ImGui::TreePop();
      }
    }

  }  // namespace

  void ShaderReflection::inspect() {
    if (root == nullptr) {
      ImGui::Text("No reflection data available");
      return;
    }

    const float textBaseWidth = ImGui::CalcTextSize("A").x;
    constexpr auto flags =
        ImGuiTableFlags_BordersV |
        ImGuiTableFlags_BordersOuterH |
        ImGuiTableFlags_RowBg;
    const float locationWidth = textBaseWidth * 3.5f;
    constexpr auto locationFlags =
        ImGuiTableColumnFlags_WidthFixed |
        ImGuiTableColumnFlags_NoHide |
        ImGuiTableColumnFlags_NoResize;

    if (!ImGui::BeginTable("##ShaderReflection", 10, flags)) {
      return;
    }

    ImGui::TableSetupColumn(
        "Name", ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn(
        "Type", ImGuiTableColumnFlags_WidthFixed, textBaseWidth * 25.0f);
    ImGui::TableSetupColumn("PC?", locationFlags, locationWidth);
    ImGui::TableSetupColumn("SET", locationFlags, locationWidth);
    ImGui::TableSetupColumn("IND", locationFlags, locationWidth);
    ImGui::TableSetupColumn("OFF", locationFlags, locationWidth);
    ImGui::TableSetupColumn("SIZ", locationFlags, locationWidth);
    ImGui::TableSetupColumn("AID", locationFlags, locationWidth);
    ImGui::TableSetupColumn("VIN", locationFlags, locationWidth);
    ImGui::TableSetupColumn("VOUT", locationFlags, locationWidth);
    ImGui::TableHeadersRow();

    for (const auto* member : root->members) {
      inspectNodeRecursive(member);
    }

    ImGui::EndTable();
  }

}  // namespace ren
