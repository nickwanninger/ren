#include "./ShaderReflection.h"

#include <imgui/imgui.h>

namespace ren {
  namespace {

    constexpr ImGuiTreeNodeFlags treeNodeFlagsBase =
        ImGuiTreeNodeFlags_SpanAllColumns |
        ImGuiTreeNodeFlags_DefaultOpen |
        ImGuiTreeNodeFlags_DrawLinesFull;

    const char* materialValueKindName(ShaderReflection::ValueKind kind) {
      using ValueKind = ShaderReflection::ValueKind;
      switch (kind) {
        case ValueKind::Scalar: return "Scalar";
        case ValueKind::Vector: return "Vector";
        case ValueKind::Matrix: return "Matrix";
        case ValueKind::Struct: return "Struct";
        case ValueKind::Array: return "Array";
        case ValueKind::Pointer: return "Pointer";
        case ValueKind::Enum: return "Enum";
        default: return "Unknown";
      }
    }

    std::string materialAttributesText(
        const std::vector<ShaderReflection::MaterialAttribute>& attributes) {
      std::string result;
      for (const auto& attribute : attributes) {
        if (!result.empty()) result += " ";
        result += "[" + attribute.name;
        if (!attribute.arguments.empty()) {
          result += "(";
          for (size_t i = 0; i < attribute.arguments.size(); ++i) {
            if (i != 0) result += ", ";
            result += attribute.arguments[i].dump();
          }
          result += ")";
        }
        result += "]";
      }
      return result;
    }

    void inspectMaterialFieldRecursive(
        const ShaderReflection::MaterialField& field) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);

      const bool isLeaf = field.fields.empty();
      auto flags = treeNodeFlagsBase;
      if (isLeaf) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
      }

      const bool open = ImGui::TreeNodeEx(
          &field,
          flags,
          "%s",
          field.name.empty() ? "<unnamed>" : field.name.c_str());

      ImGui::TableNextColumn();
      ImGui::TextUnformatted(field.typeName.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(materialValueKindName(field.kind));
      ImGui::TableNextColumn();
      ImGui::Text("%u", field.byteOffset);
      ImGui::TableNextColumn();
      ImGui::Text("%u", field.byteSize);
      ImGui::TableNextColumn();
      ImGui::Text("%u", field.alignment);
      ImGui::TableNextColumn();
      const auto attributes = materialAttributesText(field.attributes);
      ImGui::TextUnformatted(attributes.c_str());

      if (open && !isLeaf) {
        for (const auto& child : field.fields) {
          inspectMaterialFieldRecursive(child);
        }
        ImGui::TreePop();
      }
    }

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

  void ShaderReflection::MaterialSchema::inspect() const {
    ImGui::PushID(this);

    ImGui::Text("Material type: %s", typeName.c_str());
    ImGui::Text("Material layout: %u bytes, alignment %u", byteSize, alignment);
    ImGui::Text(
        "Push constant: %s (%u bytes)",
        pushConstantName.empty() ? "<unnamed>" : pushConstantName.c_str(),
        pushConstantByteSize);
    ImGui::Text(
        "Pointers: instances @ %u, objects @ %u, materials @ %u",
        instancePointerOffset,
        objectPointerOffset,
        materialPointerOffset);

    constexpr auto tableFlags =
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("##MaterialSchema", 7, tableFlags)) {
      ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed);
      ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_WidthFixed);
      ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed);
      ImGui::TableSetupColumn("Align", ImGuiTableColumnFlags_WidthFixed);
      ImGui::TableSetupColumn("Attributes", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableHeadersRow();

      for (const auto& field : fields) {
        inspectMaterialFieldRecursive(field);
      }
      ImGui::EndTable();
    }

    ImGui::PopID();
  }

  void ShaderReflection::inspect() {
    if (materialSchema.isSome()) {
      ImGui::SeparatorText("Material Schema");
      materialSchema.unwrap().inspect();
      ImGui::SeparatorText("Raw Reflection");
    }

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
