#include <ren/types.h>
#include <ren/core/AutoPlugin.h>
#include <set>

namespace ren {

  static std::set<ren::AutoPlugin *> g_plugins;


  ren::AutoPlugin::AutoPlugin(const char *name)
      : name(name) {
    fmt::print("Registering plugin: {}\n", name);
    // Register this plugin in the global set of plugins.
    g_plugins.insert(this);
  }


  void AutoPlugin::registerPlugins(ren::Application &app) {
    for (auto *plugin : g_plugins) {
      fmt::println("Loading plugin: {}\n", plugin->name);
      plugin->load(app);
    }
  }

}  // namespace ren