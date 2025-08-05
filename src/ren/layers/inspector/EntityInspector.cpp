#include <ren/layers/inspector/Inspector.h>

#include <ren/core/Components.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <ren/types.h>

#include <ImGuizmo/ImGuizmo.h>


namespace ren {


  static bool dragParts(const char *label, const char *parts, float *values, float speed,
                        float resetValue) {
    auto &style = ImGui::GetStyle();
    int nparts = strlen(parts);
    bool changed = false;

    ImGui::PushID(label);

    ImGui::BeginGroup();
    ImGui::PushMultiItemsWidths(nparts, ImGui::CalcItemWidth());
    // auto partWidth = ImGui::GetItemRectSize().x / nparts;

    ImVec2 buttonSize = ImVec2{ImGui::GetFrameHeight(), ImGui::GetFrameHeight()};
    for (int i = 0; i < nparts; i++) {
      ImGui::PushID(i);
      // float buttonWidth = ImGui::GetFrameHeight(); // Use frame height for square button
      // float spacingWidth = style.ItemInnerSpacing.x;
      // float dragFloatWidth = partWidth - buttonWidth - spacingWidth;

      // ImGui::SetNextItemWidth(buttonWidth);
      if (i > 0) ImGui::SameLine(0, style.ItemInnerSpacing.x);
      // render a reset button
      char buttonMessage[2] = {parts[i], '\0'};
      if (ImGui::Button(buttonMessage, buttonSize)) {
        values[i] = resetValue;
        changed = true;
      }
      // ImGui::SetNextItemWidth(dragFloatWidth);
      ImGui::SameLine(0, style.ItemInnerSpacing.x);
      changed |= ImGui::DragFloat("", &values[i], speed);
      ImGui::PopID();
      ImGui::PopItemWidth();
    }

    ImGui::EndGroup();

    ImGui::SameLine(0, style.ItemInnerSpacing.x);
    ImGui::Text("%s", label);

    ImGui::PopID();
    return changed;
  }



  static void renderInspector(Entity &entity, comp::ID &comp) {
    if (ImGui::CollapsingHeader("ID", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Text("UUID: %llu", (u64)comp.uuid);
    }
  }

  static void renderInspector(Entity &entity, comp::Name &comp) {
    // Don't render anything. we already render the name at the top of the inspector.
  }


  static void renderInspector(Entity &entity, comp::Transform &transform) {
    static glm::vec3 lastEuler = glm::vec3(0.0f);
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
      dragParts("Translation", "XYZ", glm::value_ptr(transform.translation), 0.1f, 0.0f);

      // Editing rotation is annoying.
      glm::vec3 currentEuler = glm::degrees(glm::eulerAngles(transform.rotation));
      float diff = glm::length(currentEuler - lastEuler);
      if (diff > 0.1f) { lastEuler = currentEuler; }
      glm::vec3 editEuler = lastEuler;
      if (dragParts("Rotation", "XYZ", glm::value_ptr(editEuler), 0.1f, 0.0f)) {
        glm::vec3 delta = editEuler - lastEuler;
        glm::quat deltaQuat = glm::quat(glm::radians(delta));
        transform.rotation = transform.rotation * deltaQuat;
        lastEuler = editEuler;
      }

      dragParts("Quaternion", "XYZW", glm::value_ptr(transform.rotation), 0.1f, 1.0f);

      dragParts("Scale", "XYZ", glm::value_ptr(transform.scale), 0.1f, 1.0f);
    }
  }

  static void renderInspector(Entity &entity, comp::Mesh &comp) {
    if (ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen)) {
      auto &mesh = comp.mesh;
      ImGui::Text("Mesh: %s", mesh->getName().c_str());
      ImGui::Text("Vertex Count: %u", mesh->getVertexCount());
      ImGui::Text("Index Count: %u", mesh->getIndexCount());
    }
  }

  static void renderInspector(Entity &entity, comp::Material &comp) {
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
      auto &material = comp.material;
      if (material) {
        material->inspect();
      } else {
        ImGui::Text("No material assigned.");
      }
    }
  }



  void renderEntityInspector(Entity &entity) {
    if (!entity) {
      ImGui::Text("No entity selected.");
      return;
    }

    ImGui::PushID((u64)getUUID(entity));

    auto &name = entity.get_mut<comp::Name>();

    // display an editor for the entity name
    char nameBuffer[256];
    strncpy(nameBuffer, name.name.c_str(), sizeof(nameBuffer) - 1);
    nameBuffer[sizeof(nameBuffer) - 1] = '\0';  // Ensure null-termination
    if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) {
      name.name = std::string(nameBuffer);
    }


#define COMP(type)                          \
  if (auto *comp = entity.try_get_mut<type>()) { \
    ImGui::PushID(&comp);                   \
    renderInspector(entity, *comp);         \
    ImGui::PopID();                         \
  }
#include <ren/core/Components.inc>


    ImGui::PopID();
  }  // namespace ren



}  // namespace ren