#include <ren/edit/SceneEditor.h>

#include <algorithm>
#include <stdexcept>

namespace ren::edit {

  //=========================
  // Edit addresses
  //=========================

  EditAddress EditAddress::operator/(std::string segment) const {
    EditAddress result = *this;
    result.segments.push_back(std::move(segment));
    return result;
  }


  bool EditAddress::contains(const EditAddress& other) const {
    return other.segments.size() >= segments.size() && std::equal(segments.begin(), segments.end(), other.segments.begin());
  }


  std::string EditAddress::key() const { return nlohmann::json(segments).dump(); }


  std::string EditAddress::display() const {
    std::string result;
    for (size_t i = 0; i < segments.size(); ++i) {
      if (i != 0) {
        result += "/";
      }
      result += segments[i];
    }
    return result;
  }


  // Parent and child snapshots in one edit make replay order ambiguous. Keep
  // custom editor batches at one field depth, or use independent sibling paths.
  static bool overlaps(const EditAddress& first, const EditAddress& second) { return first.contains(second) || second.contains(first); }




  //=========================
  // Edit sessions
  //=========================

  EditHistory::EditHistory(PropertyDocument& document)
      : document(document) {}


  void EditHistory::beginFrame() { ownerVisible = false; }


  void EditHistory::endFrame() {
    if (pending && !ownerVisible) {
      commit();
    }
  }


  void EditHistory::touch(std::uint64_t owner) {
    if (isOwner(owner)) {
      ownerVisible = true;
    }
  }


  void EditHistory::begin(std::uint64_t owner, std::string label) {
    if (isOwner(owner)) {
      touch(owner);
      return;
    }

    commit();
    pending = Edit{.label = std::move(label)};
    pendingOwner = owner;
    ownerVisible = true;
  }


  void EditHistory::update(std::uint64_t owner, const std::string& label, const std::vector<PropertyWrite>& writes) {
    begin(owner, label);

    // Capture snapshots before the document changes. Keep new records local until
    // the entire batch succeeds, so a failed candidate leaves no partial record.
    auto changes = pending->changes;
    for (const auto& write : writes) {
      bool known = false;
      for (const auto& change : changes) {
        if (change.address == write.address) {
          known = true;
        } else if (overlaps(change.address, write.address)) {
          throw std::runtime_error("Overlapping edit paths: " + write.address.display());
        }
      }
      if (!known) {
        changes.push_back(FieldChange{.address = write.address, .before = document.read(write.address)});
      }
    }

    document.apply(writes);
    pending->changes = std::move(changes);
    if (!writes.empty()) {
      ++revision;
    }
  }


  void EditHistory::commit() {
    if (!pending) {
      return;
    }

    Edit result{.label = pending->label};
    for (const auto& change : pending->changes) {
      auto after = document.read(change.address);
      if (change.before != after) {
        result.changes.push_back(FieldChange{change.address, change.before, std::move(after)});
      }
    }

    // Only effective edits discard redo. Canceling or returning to the starting
    // value therefore preserves the previous history branch.
    if (!result.changes.empty()) {
      entries.erase(entries.begin() + cursor, entries.end());
      entries.push_back(std::move(result));
      cursor = entries.size();
    }
    pending.reset();
  }


  void EditHistory::cancel() {
    if (!pending) {
      return;
    }

    std::vector<PropertyWrite> writes;
    for (const auto& change : pending->changes) {
      writes.push_back({change.address, change.before});
    }
    document.apply(writes);
    if (!writes.empty()) {
      ++revision;
    }
    pending.reset();
  }


  void EditHistory::execute(std::string label, const std::vector<PropertyWrite>& writes) {
    commit();
    update(0, label, writes);
    commit();
  }




  //=========================
  // History replay
  //=========================

  bool EditHistory::undo() {
    if (pending) {
      cancel();
      return true;
    }
    if (cursor == 0) {
      return false;
    }

    std::vector<PropertyWrite> writes;
    for (const auto& change : entries[cursor - 1].changes) {
      writes.push_back({change.address, change.before});
    }
    document.apply(writes);
    --cursor;
    ++revision;
    return true;
  }


  bool EditHistory::redo() {
    if (pending) {
      cancel();
      return true;
    }
    if (cursor == entries.size()) {
      return false;
    }

    std::vector<PropertyWrite> writes;
    for (const auto& change : entries[cursor].changes) {
      writes.push_back({change.address, change.after});
    }
    document.apply(writes);
    ++cursor;
    ++revision;
    return true;
  }


  nlohmann::json EditHistory::read(const EditAddress& address) const { return document.read(address); }


  bool EditHistory::isOwner(std::uint64_t owner) const { return pending.has_value() && pendingOwner == owner; }


  bool EditHistory::isActive() const { return pending.has_value(); }


  bool EditHistory::canUndo() const { return pending.has_value() || cursor > 0; }


  bool EditHistory::canRedo() const { return pending.has_value() || cursor < entries.size(); }


  std::size_t EditHistory::getCursor() const { return cursor; }


  std::uint64_t EditHistory::getRevision() const { return revision; }


  const std::vector<Edit>& EditHistory::getEntries() const { return entries; }




  //=========================
  // Editor context and state
  //=========================

  void EditorStateStore::clear() { values.clear(); }


  EditContext::EditContext(EditHistory& history, EditorStateStore& states, EditAddress address, std::uint64_t widget)
      : history(history)
      , states(states)
      , address(std::move(address))
      , widget(widget) {}


  nlohmann::json EditContext::read() const { return history.read(address); }


  bool EditContext::submit(const nlohmann::json& candidate, EditInteraction interaction) { return submitBatch({{address, candidate}}, interaction); }


  bool EditContext::submitBatch(const std::vector<PropertyWrite>& candidates, EditInteraction interaction) {
    auto& feedback = state<Feedback>();
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
        history.update(widget, "Edit " + address.display(), candidates);
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
    return EditContext(history, states, address / std::move(field), childWidget);
  }


  std::uint64_t EditContext::getRevision() const { return history.getRevision(); }


  const EditAddress& EditContext::getAddress() const { return address; }


  const std::string& EditContext::getError() const { return state<Feedback>().error; }

}  // namespace ren::edit
