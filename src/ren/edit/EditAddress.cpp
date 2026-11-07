#include <ren/edit/EditAddress.h>

#include <json/json.hpp>
#include <algorithm>

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

}  // namespace ren::edit
