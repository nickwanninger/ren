#pragma once

#include <ren/assets/importers/ImportContext.h>
#include <concepts>
#include <functional>

namespace ren {

  // Converts one source file into derived bundles. The registry constructs one importer per import
  // and gives it the parsed options.
  //
  // A derived class supplies three things for registerImporter():
  //   struct Options                                    Default-constructible. Converts to and from JSON.
  //   static std::vector<std::string> getExtensions()   Lowercase, with the dot: ".png".
  //   explicit Derived(Options options)
  class Importer {
   public:
    virtual ~Importer() = default;

    // Read inputs and write bundles through the context. Throws if the source is invalid.
    virtual void import(ImportContext &context) = 0;
  };


  // One registry entry. The registry owns it for the life of the program.
  struct ImporterInfo {
    // The name that a sidecar stores in its `importer` field.
    std::string name;
    std::vector<std::string> extensions;

    // Validate `options`, fill the missing defaults into it, and construct the importer.
    // Throws an error that names the option if a key is unknown or a value has the wrong type.
    std::function<Box<Importer>(json &options)> create;
  };


  namespace import_detail {

    // Add an entry to the registry. Throws if the name exists.
    void addImporter(ImporterInfo info);

    // Compare `options` to the JSON form of the default options. Throws an error that names the
    // option if a key is absent from `defaults` or its value has a different JSON type.
    void verifyOptions(const json &defaults, const json &options);

  }  // namespace import_detail


  template <typename T>
  concept ImporterType = std::derived_from<T, Importer> && std::default_initializable<typename T::Options> &&
                         std::constructible_from<T, typename T::Options> && requires(json value, typename T::Options options) {
                           { T::getExtensions() } -> std::convertible_to<std::vector<std::string>>;
                           value = options;
                           value.get_to(options);
                         };


  // Register an importer class under `name`. The first importer that claims an extension is the
  // default for files with that extension; a sidecar can name any registered importer.
  // Call this before the first import. Throws if the name exists.
  template <ImporterType T>
  void registerImporter(std::string name) {
    ImporterInfo info;
    info.name = std::move(name);
    info.extensions = T::getExtensions();

    info.create = [](json &options) -> Box<Importer> {
      using Options = typename T::Options;
      import_detail::verifyOptions(json(Options{}), options);

      // Start from the defaults, so that absent keys keep their default values.
      Options parsed = options.template get<Options>();
      options = parsed;
      return makeBox<T>(std::move(parsed));
    };

    import_detail::addImporter(std::move(info));
  }


  // Register the importers that ren supplies. AssetManager calls this. More calls have no effect.
  void registerBuiltinImporters(void);

  // Find an importer by its registered name. Returns null if the name is unknown.
  const ImporterInfo *findImporter(std::string_view name);

  // Find the default importer for the extension of `path`. Extensions are not case-sensitive.
  // Returns null if no importer supports the extension.
  const ImporterInfo *findImporterForPath(const std::filesystem::path &path);

}  // namespace ren
