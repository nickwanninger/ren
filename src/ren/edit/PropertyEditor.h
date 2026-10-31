#pragma once

#include <ren/edit/EditAddress.h>
#include <json/json.hpp>
#include <vector>

namespace ren::edit {

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

}  // namespace ren::edit
