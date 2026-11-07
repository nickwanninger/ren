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
  // not know what they change. There are three ways to add an entry:
  //
  //   execute(command)   The history runs command->redo(), then records it.
  //                      Use it for a change that happens in one step, such as
  //                      a menu action or a button.
  //   record(command)    The history records the command but does not run it.
  //                      Use it when the caller already made the change.
  //   open(command)      The caller applies changes live over many frames, and
  //                      commit() records one entry, or cancel() undoes it. Use
  //                      it for an interactive edit, such as a drag or typing.
  //                      track() drives this from widget flags.
  //
  // execute(), record() and open() first commit an open command, so an edit in
  // progress and a new command stay two entries in the order they happened.
  // An editor that draws widgets calls endFrame() once per frame.
  class EditHistory {
   public:
    // Commits the open command if its owner did not call touch() since the last
    // endFrame(), for example after a selection change hid the widget during an
    // edit. Call once per frame, after the document's UI is drawn. Headless use
    // does not need it.
    void endFrame();

    // Marks `owner` as drawn this frame. Each widget that can own the open
    // command calls this when it draws; track(), editValue() and EditContext
    // call it for you.
    void touch(std::uint64_t owner);

    // Commits the open command, then runs command->redo() and records the
    // command. The change happens during this call.
    void execute(Box<Command> command);

    // Commits the open command, then records a command whose change the caller
    // already made. The history does not run it now; undo() and redo() run it later.
    void record(Box<Command> command);

    // Commits any open command, then holds `command` open for `owner`. The
    // history does not run it: the caller applies changes live, and the command's
    // constructor must already hold the state from before the change. commit()
    // records it as one entry; cancel() calls undo() and drops it. Returns the
    // open command, so the caller can add to it while it stays open.
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

    // Closes the open command and calls its finish(). If finish() returns true,
    // the history records the command; if false, it drops the command and keeps
    // the redo branch. No open command is a no-op.
    void commit();

    // Calls undo() on the open command and drops it, so the data returns to its
    // state before open(). Nothing is recorded. No open command is a no-op.
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

    // Commits the open command, then remembers the current entry as the one
    // that matches the saved file. Call it after the document is written.
    void markSaved();

    // Reports whether the data differs from the last markSaved(): a different
    // entry is current, or a command is open. A new history is clean.
    bool isDirty() const;

   private:
    // Discards the redo branch and appends one entry with a new ID.
    void push(Box<Command> command);

    // Returns the ID of the last applied entry, or 0 at the start of the list.
    std::uint64_t getCurrentID() const;

    // `entryIDs[i]` identifies `entries[i]`. IDs start at 1 and never repeat,
    // so an entry that replaces an undone entry gets a different ID.
    std::vector<Box<Command>> entries;
    std::vector<std::uint64_t> entryIDs;
    std::uint64_t nextEntryID = 1;
    std::uint64_t savedID = 0;
    std::size_t cursor = 0;
    Box<Command> pending;
    std::uint64_t pendingOwner = 0;
    std::uint64_t revision = 0;
    bool ownerVisible = false;
  };

}  // namespace ren::edit
