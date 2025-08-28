#pragma once

#include <ren/core/Application.h>

// This file defines an interface by which the user of ren can declare a plugin
// with a global macro.  The goal of this is to allow the user to easily add
// plugins to the flecs world without having to modify a central location.

namespace ren {

  class AutoPlugin {
   public:
    AutoPlugin(const char *name);

    virtual void load(ren::Application &app) {};


    static void registerPlugins(ren::Application &app);

    private:
    const char *name;
  };


  using AutoPluginCallback = std::function<void(ren::Application &app)>;

  class AutoPluginRegistration : public AutoPlugin {
   public:
    AutoPluginRegistration(const char *name, AutoPluginCallback callback)
        : AutoPlugin(name)
        , callback(callback) {}

    inline void load(ren::Application &app) override {
      // Call the callback function with the application instance.
      callback(app);
    }

   private:
    AutoPluginCallback callback;
  };

}  // namespace ren


#define REN_PLUGIN_TOKENPASTE(x, y) x ## y
#define REN_PLUGIN_TOKENPASTE2(x, y) REN_PLUGIN_TOKENPASTE(x, y)

#define REN_PLUGIN(name, callback) \
  static ren::AutoPluginRegistration REN_PLUGIN_TOKENPASTE2(g_autoPluginRegistration_, __LINE__) (name, callback);