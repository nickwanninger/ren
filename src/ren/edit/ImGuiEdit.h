#pragma once

#include <ren/types.h>
#include <ren/edit/EditHistory.h>
#include <ren/core/ui/Widgets.h>
#include <imgui.h>
#include <string>

namespace ren::edit {

  // Reads the last ImGui item's state as one frame of an interaction. After
  // EndGroup(), the last item is the whole group. Escape while the item is
  // active, or on the frame that it deactivates, cancels the interaction.
  EditInteraction interactionFromLastItem(bool changed);


  // Draws an editor for one plain value and records its edits in `history`.
  // `draw` receives the UI and a copy of the value and returns true when it changed the
  // copy. It can draw several items; they act as one control. An edit over many
  // frames, such as a drag or typing, becomes one SetValue entry, and Escape
  // restores the start value.
  // Returns true when `draw` changed the value in this frame.
  template <typename T, typename Draw>
  bool editValue(eui::Ui& ui, EditHistory& history, const char* label, T& value, Draw&& draw) {
    ImGui::PushID(label);

    // The group's own item ID is 0 unless a child is active, so use an ID from
    // the label scope. It stays the same on each frame of the interaction.
    const auto owner = ImGui::GetID("##edit");

    T candidate = value;
    ImGui::BeginGroup();
    const bool changed = draw(ui, candidate);
    ImGui::EndGroup();

    history.track(
        owner, interactionFromLastItem(changed), [&] { return makeBox<SetValue<T>>(std::string("Edit ") + label, value); },
        [&] { value = candidate; });

    ImGui::PopID();
    return changed;
  }


  // Draws the default Ui::inspect widget for `value` and records its edits.
  template <typename T>
  bool editValue(eui::Ui& ui, EditHistory& history, const char* label, T& value) {
    return editValue(ui, history, label, value, [label](eui::Ui& ui, T& candidate) { return ui.inspect(label, candidate); });
  }

}  // namespace ren::edit
