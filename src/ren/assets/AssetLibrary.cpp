#include <ren/assets/AssetLibrary.h>

namespace ren {

  AssetLibrary::AssetLibrary(AssetSource &source)
      : source(source) {}



  //=========================
  // Scan
  //=========================

  void AssetLibrary::scan(void) {
    std::scoped_lock lock(mutex);
    generation++;
    // TODO: visit Native and Source files, read records, and remove entries that the scan did not visit.
  }



  //=========================
  // Queries
  //=========================

  ref<const AssetEntry> AssetLibrary::find(AssetID id) const {
    std::scoped_lock lock(mutex);
    auto it = entries.find(id);
    return it != entries.end() ? it->second : nullptr;
  }


  ref<const AssetEntry> AssetLibrary::findByPath(std::string_view path) const {
    std::scoped_lock lock(mutex);
    auto it = byPath.find(std::string(path));
    if (it == byPath.end()) {
      return nullptr;
    }
    return entries.at(it->second);
  }


  void AssetLibrary::forEach(const std::function<void(const ref<const AssetEntry> &)> &fn) const {
    std::scoped_lock lock(mutex);
    for (const auto &[id, entry] : entries) {
      fn(entry);
    }
  }

}  // namespace ren
