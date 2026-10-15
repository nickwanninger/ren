#pragma once

#include <ren/core/Window.h>
#include <ren/core/FramerateCounter.h>
#include <ren/renderer/Viewport.h>

namespace editor {

  // The editor's main window: a dockspace with a menu bar, the log, the
  // shader cache panel and one scene viewport.
  class EditorWindow : public ren::Window {
   protected:
    void onInitialize(void) override;
    void onUpdate(float dt) override;
    void onRender(ren::FrameContext &frame) override;

   private:
    // Created in onInitialize(), because a viewport needs the device.
    ren::Box<ren::EditorViewport> m_viewport;
    ren::FramerateCounter m_framerateCounter;
    float m_deltaTime = 0.0f;

    // The global camera's matrices for this frame, for the debug lines.
    glm::mat4 m_viewMatrix{1.0f};
    glm::mat4 m_projMatrix{1.0f};
  };

}  // namespace editor
