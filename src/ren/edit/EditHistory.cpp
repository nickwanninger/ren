#include <ren/edit/EditHistory.h>

#include <utility>

namespace ren::edit {

  //=========================
  // Open commands
  //=========================

  void EditHistory::endFrame() {
    if (pending && !ownerVisible) {
      commit();
    }

    // Reset for the next frame. open() sets the flag again, so a command opened
    // during this frame survives this frame's endFrame().
    ownerVisible = false;
  }


  void EditHistory::touch(std::uint64_t owner) {
    if (isOwner(owner)) {
      ownerVisible = true;
    }
  }


  Command& EditHistory::open(Box<Command> command, std::uint64_t owner) {
    commit();
    pending = std::move(command);
    pendingOwner = owner;
    ownerVisible = true;
    return *pending;
  }


  void EditHistory::commit() {
    if (!pending) {
      return;
    }

    // Only effective commands discard redo. An edit that returns to its start
    // value therefore preserves the previous history branch.
    auto command = std::move(pending);
    if (command->finish()) {
      push(std::move(command));
    }
  }


  void EditHistory::cancel() {
    if (!pending) {
      return;
    }

    auto command = std::move(pending);
    command->undo();
    ++revision;
  }


  void EditHistory::markChanged() { ++revision; }




  //=========================
  // Complete commands
  //=========================

  void EditHistory::execute(Box<Command> command) {
    commit();
    command->redo();
    push(std::move(command));
    ++revision;
  }


  void EditHistory::record(Box<Command> command) {
    commit();
    push(std::move(command));
    ++revision;
  }


  void EditHistory::push(Box<Command> command) {
    entries.erase(entries.begin() + cursor, entries.end());
    entries.push_back(std::move(command));
    cursor = entries.size();
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

    entries[cursor - 1]->undo();
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

    entries[cursor]->redo();
    ++cursor;
    ++revision;
    return true;
  }


  Command* EditHistory::getOpen() const { return pending.get(); }


  bool EditHistory::isOwner(std::uint64_t owner) const { return pending && pendingOwner == owner; }


  bool EditHistory::isOpen() const { return pending != nullptr; }


  bool EditHistory::canUndo() const { return pending || cursor > 0; }


  bool EditHistory::canRedo() const { return pending || cursor < entries.size(); }


  std::size_t EditHistory::getCursor() const { return cursor; }


  std::uint64_t EditHistory::getRevision() const { return revision; }


  const std::vector<Box<Command>>& EditHistory::getEntries() const { return entries; }

}  // namespace ren::edit
