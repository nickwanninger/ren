#pragma once

#include <ren/types.h>
#include <ren/core/Event.h>


namespace ren {

  class Application;

  class Layer {
   public:
    inline Layer(Application &app, const std::string &name)
        : app(app)
        , name(name) {}
    virtual ~Layer() = default;

    const std::string &getName(void) const { return name; }



    // ---- Event Handlers ---- //
    virtual void onAttach(void) {}
    virtual void onDetach(void) {}
    virtual void onUpdate(float deltaTime) {}
    virtual void onImguiRender(float deltaTime) {}
    virtual void onEvent(Event &event) {}

   protected:
    Application &app;
    std::string name;
  };

}  // namespace ren