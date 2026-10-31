#include <ren/edit/EditContext.h>

#include <stdexcept>

namespace ren::edit {

  //=========================
  // Editor context and state
  //=========================

  void EditorStateStore::clear() { values.clear(); }


  EditContext::EditContext(PropertyEditor& properties, EditorStateStore& states, EditAddress address, std::uint64_t widget)
      : properties(properties)
      , states(states)
      , address(std::move(address))
      , widget(widget) {}


  nlohmann::json EditContext::read() const { return properties.read(address); }


  bool EditContext::submit(const nlohmann::json& candidate, EditInteraction interaction) { return submitBatch({{address, candidate}}, interaction); }


  bool EditContext::submitBatch(const std::vector<PropertyWrite>& candidates, EditInteraction interaction) {
    auto& feedback = state<Feedback>();
    auto& history = properties.getHistory();
    const auto revision = history.getRevision();
    history.touch(widget);

    if (interaction.canceled) {
      if (history.isOwner(widget)) {
        history.cancel();
      }
      feedback.error.clear();
      return revision != history.getRevision();
    }

    if (interaction.changed) {
      try {
        for (const auto& write : candidates) {
          if (!address.contains(write.address)) {
            throw std::runtime_error("Editor write escapes field: " + write.address.display());
          }
        }
        properties.update(widget, "Edit " + address.display(), candidates);
        feedback.error.clear();
      } catch (const std::exception& error) {
        feedback.error = error.what();
      }
    }
    if ((interaction.finished || !interaction.active) && history.isOwner(widget)) {
      if (feedback.error.empty()) {
        history.commit();
      } else {
        history.cancel();
      }
    }
    return revision != history.getRevision();
  }


  EditContext EditContext::child(std::string field, std::uint64_t childWidget) const {
    return EditContext(properties, states, address / std::move(field), childWidget);
  }


  std::uint64_t EditContext::getRevision() const { return properties.getHistory().getRevision(); }


  const EditAddress& EditContext::getAddress() const { return address; }


  const std::string& EditContext::getError() const { return state<Feedback>().error; }

}  // namespace ren::edit
