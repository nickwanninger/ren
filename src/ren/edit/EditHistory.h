#pragma once

#include <ren/types.h>
#include <ren/edit/Command.h>
#include <cstdint>
#include <vector>

namespace ren::edit {

  // Describes one control's interaction. Custom editors report these events after
  // each control. Inactive controls finish their session even without a change.
  struct EditInteraction {
    bool changed = false;
    bool active = false;
    bool finished = false;
    bool canceled = false;
  };


  // Owns the undo list for one document. Entries are commands; the history does
  // not know what they change. An interactive edit, such as a drag or typing,
  // is an open command: the caller applies it live, and commit() records it as
  // one entry.
  // Call beginFrame/endFrame around editor drawing to commit disappearing controls.
  class EditHistory {
   public:
    // Starts tracking which owner of the open command draws in this frame.
    void beginFrame();

    // Commits the open command if its owner did not draw during this frame.
    void endFrame();

    // Keeps the open command of this owner alive without changing data.
    void touch(std::uint64_t owner);

    // Commits the open command, runs this command, and records it.
    void execute(Box<Command> command);

    // Commits the open command and records this command, which the caller
    // already applied. The history does not run it.
    void record(Box<Command> command);

    // Commits any open command, then holds this one open for `owner`. The caller
    // applies changes live; commit() or cancel() closes it.
    Command& open(Box<Command> command, std::uint64_t owner);

    // Runs one frame of an interactive edit from its flags. The first change
    // opens the command from `make`, each change calls `apply`, release commits,
    // and cancellation undoes the open command.
    template <typename Make, typename Apply>
    void track(std::uint64_t owner, EditInteraction interaction, Make&& make, Apply&& apply) {
      touch(owner);
      if (interaction.canceled) {
        if (isOwner(owner)) {
          cancel();
        }
        return;
      }

      if (interaction.changed) {
        if (!isOwner(owner)) {
          open(make(), owner);
        }
        apply();
        markChanged();
      }

      if ((interaction.finished || !interaction.active) && isOwner(owner)) {
        commit();
      }
    }

    // Closes the open command. finish() decides if it is recorded or dropped.
    // An unchanged command keeps the redo branch. No open command is a no-op.
    void commit();

    // Undoes the open command and drops it. No open command is a no-op.
    void cancel();

    // Cancels the open command first; otherwise undoes the previous entry.
    // Returns false when neither action is available.
    bool undo();

    // Cancels the open command first; otherwise redoes the next entry.
    // Returns false when neither action is available.
    bool redo();

    // Reports a live change to the open command, so editors reload their drafts.
    void markChanged();

    // Returns the open command, or null.
    Command* getOpen() const;

    // Identifies the owner of the open command, if one exists.
    bool isOwner(std::uint64_t owner) const;

    // Reports whether a command awaits commit or cancellation.
    bool isOpen() const;

    // Reports whether undo can cancel an open command or undo an entry.
    bool canUndo() const;

    // Reports whether redo can cancel an open command or redo an entry.
    bool canRedo() const;

    // Returns the number of applied entries. Later entries form the redo branch.
    std::size_t getCursor() const;

    // Changes on each data change and replay. Editors use it to invalidate drafts.
    std::uint64_t getRevision() const;

    // Returns recorded entries for display. Callers cannot change them.
    const std::vector<Box<Command>>& getEntries() const;

   private:
    // Discards the redo branch and appends one entry.
    void push(Box<Command> command);

    std::vector<Box<Command>> entries;
    std::size_t cursor = 0;
    Box<Command> pending;
    std::uint64_t pendingOwner = 0;
    std::uint64_t revision = 0;
    bool ownerVisible = false;
  };

}  // namespace ren::edit
