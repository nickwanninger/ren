#pragma once

#include <ren/core/Window.h>

namespace ren {



  // This window provides a basic ImGui window, and calls draw() on each frame.
  // It is a base class for windows that want just a fullscreen imgui window for
  // their content.
  class ImGuiWindow : public Window {
   public:
    // Called in the context of the fullscreen ImGui::begin()/end() block.
    virtual void draw(void) = 0;



    void onUpdate(float dt) override;
    void onRender(FrameContext &frame) override;
  };


  class LambdaImGuiWindow : public ImGuiWindow {
   public:
    LambdaImGuiWindow(std::function<void(void)> drawFunc)
        : m_drawFunc(drawFunc) {}

    void draw(void) override { m_drawFunc(); }

   private:
    std::function<void(void)> m_drawFunc;
  };
}  // namespace ren