#pragma once

#include <ren/types.h>
#include <ren/Camera.h>
#include <ren/renderer/graph/RenderGraph.h>
#include <ren/renderer/pipelines/PipelineStateObject.h>

#include <functional>
#include <imgui/imgui.h>

namespace ren {

  class Renderer;
  class CommandEncoder;

  // A viewport owns graph tasks and CPU configuration for one view.
  class Viewport {
   public:
    Viewport();
    virtual ~Viewport() = default;
    Viewport(const Viewport &) = delete;
    Viewport &operator=(const Viewport &) = delete;
    void update(float dt, glm::uvec2 size);
    GraphHandle output() const { return finalImage; }
    void inspect();
    glm::uvec2 getRenderSize() const { return viewContext->renderSize; }
    glm::mat4 view{1.0f}, projection{1.0f};

   protected:
    virtual void onUpdate(float) {}
    virtual void buildGraph();
    void ensureGraphBuilt();
    ref<ViewContext> viewContext = make<ViewContext>();
    GraphScope graph;
    GraphHandle finalImage;
    int raysPerPixel = 4;
    float focusDistance = 5.0f, lensRadius = 0.06f, renderScale = 1.0f, ssaoContrast = 1.0f;

   private:
    bool graphBuilt = false;
    PipelineStateObject forwardPSO;
  };

  /**
   * @brief Per-frame mouse state, scoped to one viewport's rectangle.
   *
   * Everything here is derived from ImGui's window hover state and the
   * viewport's rectangle, rather than from SDL's global mouse state. That
   * distinction is the whole point: ImGui knows which window owns the mouse,
   * so two viewports side by side never both react to the same drag, and a
   * popup drawn over a viewport takes the mouse away from it for free. No
   * ImGui item covers the viewport, because ImGuizmo does not start a drag
   * while an item is hovered.
   */
  struct ViewportInput {
    /// The mouse is over this viewport and no other widget has claimed it.
    bool hovered = false;
    /// This viewport is the active item -- a drag that started here, even if
    /// the cursor has since left the rectangle.
    bool active = false;

    bool leftDown = false;
    bool rightDown = false;
    bool middleDown = false;

    /// Cursor position in pixels relative to the viewport's top-left.
    glm::vec2 mousePixels{0.0f};
    /// Cursor position normalized to [0,1] over the viewport.
    glm::vec2 mouseNormalized{0.0f};
    /// Frame-over-frame cursor movement, zero unless this viewport has input.
    glm::vec2 mouseDelta{0.0f};
    /// Scroll wheel, only when hovered.
    float wheel = 0.0f;
  };


  /**
   * @brief A viewport with a fly-camera, which draws its own ImGui window.
   *
   * This is where editor-only camera state lives, so that ren::Viewport itself
   * stays free of input handling. Unlike ren::Camera, nothing here is global:
   * each EditorViewport has its own position, angles and capture state, so
   * several can coexist and only the one under the mouse responds.
   */
  class EditorViewport : public Viewport {
   public:
    /// Defaults mirror ren::Camera so an editor viewport frames a scene the
    /// same way the game camera does.
    float fovDegrees = Camera::FOV;
    float nearPlane = Camera::NEAR_PLANE;
    float farPlane = Camera::FAR_PLANE;

    /// Units per second at full tilt. ren::Camera::cameraSpeed.
    float moveSpeed = 64.0f;
    /// Radians of rotation per pixel of mouse movement. ren::Camera uses the
    /// same 0.002 against SDL's relative motion, which is also pixels.
    float lookSensitivity = 0.002f;
    /// Distance in front of the camera that the view cube orbits around.
    float orbitDistance = 8.0f;

    glm::vec3 position = {0.0f, 0.0f, 3.0f};
    /// Pitch, yaw, roll -- matching ren::Camera's convention.
    glm::vec3 angles = {0.0f, 0.0f, 0.0f};
    glm::vec3 velocity = {0.0f, 0.0f, 0.0f};

    /**
     * @brief Draw this viewport's ImGui window, and update it.
     *
     * Does the whole of phase 1 for this viewport: opens the window, measures
     * it, captures scoped mouse input, runs the camera controls, prepares the
     * graph, and records the image. The caller only needs a title and a delta
     * time.
     *
     * @return whether the window is open (false if the user closed it).
     */
    bool drawWindow(const char *title, float dt, bool *open = nullptr);

    /// The input captured during the last drawWindow() call.
    const ViewportInput &getInput() const { return input; }

    /// Draw the debug overlay showing captured input state.
    bool showInputOverlay = false;

    /// Draw extra ImGui widgets inside the viewport overlay.
    std::function<void()> overlayContent;

   protected:
    void onUpdate(float dt) override;

   private:
    /// Draw the ImGuizmo view cube in the top-right corner of the viewport,
    /// and apply its rotation to the camera.
    void drawViewCube(glm::vec2 origin, glm::vec2 extent);

    ViewportInput input;
    /// True while a right-drag look is in progress. Tracked per viewport so a
    /// drag started in one viewport does not rotate another.
    bool looking = false;
    /// Bit per ImGuiMouseButton, set while a press that started inside this
    /// viewport is held. Replaces the active state of an ImGui item.
    u32 capturedButtons = 0;
    bool showGraphInspector = false;
  };

}  // namespace ren
