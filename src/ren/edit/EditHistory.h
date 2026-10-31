#pragma once

#include <ren/edit/PropertyEditor.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ren::edit {

  // Groups related field changes into one undo step. During an active session,
  // only before snapshots exist; commit captures the final values.
  struct Edit {
    std::string label;
    std::vector<FieldChange> changes;
  };


  // Owns history for one document. begin/update/commit define one interaction;
  // cancel restores its original values. Replay uses the document without logging.
  // Call beginFrame/endFrame around editor drawing to commit disappearing controls.
  class EditHistory {
   public:
    // Binds a document that must outlive this history.
    explicit EditHistory(PropertyDocument& document);

    // Starts tracking which active editor remains visible in this frame.
    void beginFrame();

    // Commits an active session if its editor did not draw during this frame.
    void endFrame();

    // Keeps the matching active editor alive without changing document data.
    void touch(std::uint64_t owner);

    // Starts a session, or continues the same owner. Another owner commits it first.
    void begin(std::uint64_t owner, std::string label);

    // Captures each original value once, then applies a candidate batch atomically.
    // Overlapping addresses throw; repeated writes to the same address are allowed.
    void update(std::uint64_t owner, const std::string& label, const std::vector<PropertyWrite>& writes);

    // Captures final values. An unchanged session preserves history and the redo branch.
    void commit();

    // Restores original values and preserves the redo branch. No active session is a no-op.
    void cancel();

    // Performs a complete command, such as reset or JSON loading, as one undo step.
    void execute(std::string label, const std::vector<PropertyWrite>& writes);

    // Cancels an active session first; otherwise restores the previous edit.
    // Returns false when neither action is available.
    bool undo();

    // Cancels an active session first; otherwise reapplies the next edit.
    // Returns false when neither action is available.
    bool redo();

    // Returns an owned value snapshot for an editor.
    nlohmann::json read(const EditAddress& address) const;

    // Identifies the owner of the active session, if one exists.
    bool isOwner(std::uint64_t owner) const;

    // Reports whether a session awaits commit or cancellation.
    bool isActive() const;

    // Reports whether undo can cancel a session or restore an edit.
    bool canUndo() const;

    // Reports whether redo can cancel a session or reapply an edit.
    bool canRedo() const;

    // Returns the number of applied entries. Later entries form the redo branch.
    std::size_t getCursor() const;

    // Changes on document writes and replay. Editors use it to invalidate drafts.
    std::uint64_t getRevision() const;

    // Returns committed history for display. Editors cannot change its entries.
    const std::vector<Edit>& getEntries() const;

   private:
    PropertyDocument& document;
    std::vector<Edit> entries;
    std::size_t cursor = 0;
    std::optional<Edit> pending;
    std::uint64_t pendingOwner = 0;
    std::uint64_t revision = 0;
    bool ownerVisible = false;
  };


  // Describes one control's interaction. Custom editors report these events after
  // each control. Inactive controls finish their session even without a change.
  struct EditInteraction {
    bool changed = false;
    bool active = false;
    bool finished = false;
    bool canceled = false;
  };

}  // namespace ren::edit
