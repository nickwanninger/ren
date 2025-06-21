#pragma once

#include <ren/types.h>



namespace ren {

  class Layer {
   public:
    inline Layer(const std::string &name)
        : name(name) {}
    virtual ~Layer() = default;

    const std::string &getName(void) const { return name; }



    // ---- Event Handlers ---- //
    virtual void onAttach(void) {}
    virtual void onDetach(void) {}
    virtual void onUpdate(float deltaTime) {}
    virtual void onImguiRender(void) {}

   private:
    std::string name;
  };

}  // namespace ren