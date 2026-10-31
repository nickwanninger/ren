#pragma once

#include <json/json.hpp>
#include <any>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <typeindex>
#include <utility>
#include <vector>

namespace ren::edit {

  // Addresses one value in a document as a list of path segments. Each document
  // decides what its segments mean; the empty address is the document root.
  // `/` appends one segment and never splits it, so a field named "a.b" or "a/b"
  // stays one segment.
  class EditAddress {
   public:
    // Returns a child address without retaining a pointer into document data.
    EditAddress operator/(std::string segment) const;

    // Returns the segments in order, starting at the document root.
    std::span<const std::string> parts() const { return segments; }

    // Reports whether `other` is this address or an address inside it.
    bool contains(const EditAddress& other) const;

    // Returns an unambiguous key for transient editor state.
    std::string key() const;

    // Returns a readable path for history labels and errors.
    std::string display() const;

    // Compares every segment.
    bool operator==(const EditAddress& other) const = default;

   private:
    std::vector<std::string> segments;
  };


  // Supplies a candidate value at one address. Documents validate candidates.
  struct PropertyWrite {
    EditAddress address;
    nlohmann::json value;
  };


  // Connects history to an authoring model. No mutable model pointer reaches an
  // editor. Implementations validate all writes before replacing live data.
  class PropertyDocument {
   public:
    // Supports destruction through the shared document interface.
    virtual ~PropertyDocument() = default;

    // Returns an owned snapshot. Missing roots or fields throw with their address.
    virtual nlohmann::json read(const EditAddress& address) const = 0;

    // Applies a batch atomically. Invalid data throws and leaves the model intact.
    virtual void apply(const std::vector<PropertyWrite>& writes) = 0;
  };


  // Stores original and final field snapshots. Fields within an edit cannot overlap.
  struct FieldChange {
    EditAddress address;
    nlohmann::json before;
    nlohmann::json after;
  };


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


  // Owns optional state for each field, widget, and state type. Drafts and preview
  // handles stay here, outside descriptors and history. The inspector owns its
  // lifetime; cached resources release when it clears or destroys this store.
  class EditorStateStore {
   public:
    // Returns persistent state for this editor instance. State must be copyable
    // and default constructible because std::any owns it.
    template <typename T>
    T& get(const EditAddress& address, std::uint64_t widget) {
      auto& value = values[{address.key(), widget, std::type_index(typeid(T))}];
      if (!value.has_value()) {
        value = T{};
      }
      return std::any_cast<T&>(value);
    }

    // Releases drafts and caches when an inspector closes or changes documents.
    void clear();

   private:
    std::map<std::tuple<std::string, std::uint64_t, std::type_index>, std::any> values;
  };


  // Describes one control's interaction. Custom editors report these events after
  // each control. Inactive controls finish their session even without a change.
  struct EditInteraction {
    bool changed = false;
    bool active = false;
    bool finished = false;
    bool canceled = false;
  };


  // Gives one editor read-only snapshots, optional state, and a controlled write
  // path. Invalid candidates stay outside the document and report a local error.
  class EditContext {
   public:
    // Binds a field and widget within an inspector. Dependencies must outlive drawing.
    EditContext(EditHistory& history, EditorStateStore& states, EditAddress address, std::uint64_t widget);

    // Returns the current authored value as an owned snapshot.
    nlohmann::json read() const;

    // Submits a candidate and its lifecycle. Commit ends the undo step; cancel
    // restores it. Returns true if document data changes, including cancellation.
    bool submit(const nlohmann::json& candidate, EditInteraction interaction);

    // Submits several child fields as one session update. Writes must stay inside
    // this context's subtree; validation and history treat the batch atomically.
    bool submitBatch(const std::vector<PropertyWrite>& candidates, EditInteraction interaction);

    // Binds a nested field to another control within a custom editor.
    EditContext child(std::string field, std::uint64_t childWidget) const;

    // Returns transient state for this field and widget, independent of history.
    template <typename T>
    T& state() const {
      return states.get<T>(address, widget);
    }

    // Returns the document revision so custom editors can invalidate cached drafts.
    std::uint64_t getRevision() const;

    // Returns the bound address for custom editors that submit a child batch.
    const EditAddress& getAddress() const;

    // Returns the last candidate error for this editor, or an empty string.
    const std::string& getError() const;

   private:
    struct Feedback {
      std::string error;
    };

    EditHistory& history;
    EditorStateStore& states;
    EditAddress address;
    std::uint64_t widget;
  };


  // Defines a shared, stateless editor. The parent draws its label and supplies an
  // ImGui ID scope and width. Implementations draw hidden-label controls, keep
  // optional state in the context, and submit candidates instead of mutating data.
  class Editor {
   public:
    // Supports destruction through the editor interface.
    virtual ~Editor() = default;

    // Draws the control contents and reports interaction events through the context.
    virtual void draw(EditContext& context) const = 0;
  };

}  // namespace ren::edit
