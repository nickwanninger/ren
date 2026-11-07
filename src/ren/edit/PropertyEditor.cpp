#include <ren/edit/PropertyEditor.h>

#include <stdexcept>
#include <utility>

namespace ren::edit {

  // Parent and child snapshots in one edit make replay order ambiguous. Keep
  // custom editor batches at one field depth, or use independent sibling paths.
  static bool overlaps(const EditAddress& first, const EditAddress& second) { return first.contains(second) || second.contains(first); }




  //=========================
  // JSON diff action
  //=========================

  JsonDiffAction::JsonDiffAction(PropertyDocument& document, std::string label)
      : document(document)
      , label(std::move(label)) {}


  std::string JsonDiffAction::getLabel() const { return label; }


  void JsonDiffAction::redo() {
    std::vector<PropertyWrite> writes;
    for (const auto& change : changes) {
      writes.push_back({change.address, change.after});
    }
    document.apply(writes);
  }


  void JsonDiffAction::undo() {
    std::vector<PropertyWrite> writes;
    for (const auto& change : changes) {
      writes.push_back({change.address, change.before});
    }
    document.apply(writes);
  }


  bool JsonDiffAction::finish() {
    std::vector<FieldChange> result;
    for (auto& change : changes) {
      auto after = document.read(change.address);
      if (change.before != after) {
        result.push_back(FieldChange{change.address, std::move(change.before), std::move(after)});
      }
    }
    changes = std::move(result);
    return !changes.empty();
  }


  const std::vector<FieldChange>& JsonDiffAction::getChanges() const { return changes; }


  std::vector<FieldChange> JsonDiffAction::withBefores(const std::vector<PropertyWrite>& writes) const {
    auto result = changes;
    for (const auto& write : writes) {
      bool known = false;
      for (const auto& change : result) {
        if (change.address == write.address) {
          known = true;
        } else if (overlaps(change.address, write.address)) {
          throw std::runtime_error("Overlapping edit paths: " + write.address.display());
        }
      }
      if (!known) {
        result.push_back(FieldChange{.address = write.address, .before = document.read(write.address)});
      }
    }
    return result;
  }




  //=========================
  // Property sessions
  //=========================

  PropertyEditor::PropertyEditor(PropertyDocument& document, EditHistory& history)
      : document(document)
      , history(history) {}


  void PropertyEditor::update(std::uint64_t owner, const std::string& label, const std::vector<PropertyWrite>& writes) {
    // The history owns the session. Look it up on each call, because the history
    // can close it between calls: at endFrame, on undo, or for another owner.
    auto* session = history.isOwner(owner) ? dynamic_cast<JsonDiffAction*>(history.getOpen()) : nullptr;
    if (session == nullptr || &session->document != &document) {
      session = &static_cast<JsonDiffAction&>(history.open(makeBox<JsonDiffAction>(document, label), owner));
    }

    // Capture snapshots before the document changes. Keep them only after the
    // whole batch succeeds.
    auto changes = session->withBefores(writes);
    document.apply(writes);
    session->changes = std::move(changes);
    if (!writes.empty()) {
      history.markChanged();
    }
  }


  void PropertyEditor::execute(const std::string& label, const std::vector<PropertyWrite>& writes) {
    history.commit();
    update(0, label, writes);
    history.commit();
  }


  nlohmann::json PropertyEditor::read(const EditAddress& address) const { return document.read(address); }


  EditHistory& PropertyEditor::getHistory() const { return history; }

}  // namespace ren::edit
