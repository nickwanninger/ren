#pragma once

#include <ren/edit/EditAddress.h>
#include <ren/edit/EditHistory.h>
#include <json/json.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace ren::edit {

  // Supplies a candidate value at one address. Documents validate candidates.
  struct PropertyWrite {
    EditAddress address;
    nlohmann::json value;
  };


  // Exposes a document's data as JSON values at addresses, for the generic
  // property editors. A document implements this only when it wants them.
  // Implementations validate all writes before replacing live data.
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


  // Records property writes as before and after snapshots. While it is open, it
  // holds only before snapshots; finish() reads the final values.
  class JsonDiffAction final : public Command {
   public:
    // Binds a document that must outlive this command.
    JsonDiffAction(PropertyDocument& document, std::string label);

    std::string getLabel() const override;
    void redo() override;
    void undo() override;

    // Reads the final values and drops unchanged fields. Returns false when no
    // field changed.
    bool finish() override;

    // Returns the recorded fields. After finish(), each has before and after values.
    const std::vector<FieldChange>& getChanges() const;

   private:
    friend class PropertyEditor;

    // Returns the changes with before snapshots added for new addresses. Throws
    // for overlapping addresses. The caller keeps the result only after the
    // document accepts the batch, so a failed batch leaves no record.
    std::vector<FieldChange> withBefores(const std::vector<PropertyWrite>& writes) const;

    PropertyDocument& document;
    std::string label;
    std::vector<FieldChange> changes;
  };


  // Turns property write batches into undo entries. One interaction is a
  // session: the first update opens a JsonDiffAction in the history, and the
  // history's commit() or cancel() closes it.
  class PropertyEditor {
   public:
    // Binds a document and a history that must outlive this editor.
    PropertyEditor(PropertyDocument& document, EditHistory& history);

    // Captures each original value once, then applies a candidate batch atomically.
    // Overlapping addresses throw; repeated writes to the same address are allowed.
    // A different owner commits the open command first.
    void update(std::uint64_t owner, const std::string& label, const std::vector<PropertyWrite>& writes);

    // Performs a complete edit, such as reset or JSON loading, as one undo step.
    void execute(const std::string& label, const std::vector<PropertyWrite>& writes);

    // Returns an owned value snapshot for an editor.
    nlohmann::json read(const EditAddress& address) const;

    // Returns the history that records this editor's sessions.
    EditHistory& getHistory() const;

   private:
    PropertyDocument& document;
    EditHistory& history;
  };

}  // namespace ren::edit
