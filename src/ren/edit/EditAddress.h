#pragma once

#include <span>
#include <string>
#include <vector>
#include <concepts>

namespace ren::edit {

  // Addresses one value in a document as a list of path segments. Each document
  // decides what its segments mean; the empty address is the document root.
  // `/` appends one segment and never splits it, so a field named "a.b" or "a/b"
  // stays one segment.
  class EditAddress {
   public:
    template <typename... Segments>
      requires(std::convertible_to<Segments, std::string> && ...)
    EditAddress(Segments&&... segments)
        : segments{std::string(std::forward<Segments>(segments))...} {}

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

}  // namespace ren::edit
