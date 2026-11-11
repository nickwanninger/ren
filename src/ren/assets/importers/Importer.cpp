#include "Importer.h"
#include "MeshImporter.h"
#include "TextureImporter.h"

#include <algorithm>
#include <map>
#include <mutex>

namespace ren {

  // The entries by name, and the default entry for each extension. Entries are never removed,
  // so pointers to them stay valid.
  struct ImporterRegistry {
    std::mutex mutex;
    std::map<std::string, ImporterInfo, std::less<>> importers;
    std::map<std::string, const ImporterInfo *, std::less<>> byExtension;
  };

  static ImporterRegistry &getRegistry(void) {
    static ImporterRegistry registry;
    return registry;
  }


  void import_detail::addImporter(ImporterInfo info) {
    auto &registry = getRegistry();
    std::scoped_lock lock(registry.mutex);

    auto name = info.name;
    auto [it, inserted] = registry.importers.emplace(name, std::move(info));
    if (!inserted) {
      throw std::runtime_error("Importer is already registered: " + name);
    }

    // The first importer keeps the extension.
    for (const auto &extension : it->second.extensions) {
      registry.byExtension.emplace(extension, &it->second);
    }
  }


  void import_detail::verifyOptions(const json &defaults, const json &options) {
    for (auto it = options.begin(); it != options.end(); ++it) {
      const auto &key = it.key();
      if (!defaults.contains(key)) {
        throw std::runtime_error("Unsupported import option: " + key);
      }

      // JSON does not separate 2 from 2.0, so an integer is valid for a float option.
      const auto &value = it.value();
      const auto &expected = defaults.at(key);
      bool compatible = value.type() == expected.type() || (value.is_number_integer() && expected.is_number_integer()) ||
                        (value.is_number() && expected.is_number_float());
      if (!compatible) {
        throw std::runtime_error("Invalid import option type: " + key);
      }
    }
  }


  void registerBuiltinImporters(void) {
    static std::once_flag once;
    std::call_once(once, []() {
      registerImporter<TextureImporter>("TextureImporter");
      registerImporter<MeshImporter>("MeshImporter");
    });
  }


  const ImporterInfo *findImporter(std::string_view name) {
    auto &registry = getRegistry();
    std::scoped_lock lock(registry.mutex);

    auto it = registry.importers.find(name);
    return it == registry.importers.end() ? nullptr : &it->second;
  }


  const ImporterInfo *findImporterForPath(const std::filesystem::path &path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::tolower(c); });

    auto &registry = getRegistry();
    std::scoped_lock lock(registry.mutex);

    auto it = registry.byExtension.find(extension);
    return it == registry.byExtension.end() ? nullptr : it->second;
  }

}  // namespace ren
