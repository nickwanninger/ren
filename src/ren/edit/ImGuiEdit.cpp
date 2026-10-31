#include <ren/edit/ImGuiEdit.h>

namespace ren::edit {

  EditInteraction interactionFromLastItem(bool changed) {
    const bool active = ImGui::IsItemActive();
    const bool finished = ImGui::IsItemDeactivated();
    return EditInteraction{
        .changed = changed,
        .active = active,
        .finished = finished,
        .canceled = (active || finished) && ImGui::IsKeyPressed(ImGuiKey_Escape),
    };
  }

}  // namespace ren::edit
