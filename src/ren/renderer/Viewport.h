#pragma once

#include <ren/types.h>
#include <ren/Camera.h>
#include <ren/renderer/graph/RenderGraph.h>
#include <ren/renderer/pipelines/PipelineStateObject.h>

namespace ren {

  class Renderer;

  /**
   * @brief A single view of a scene, rendered by its own render graph.
   *
   * A Viewport owns everything needed to turn a camera into an image: the
   * render graph that describes the passes, the images that graph allocates,
   * and the size those images are allocated at. It is deliberately ignorant of
   * *where* that image ends up -- an editor viewport hands it to ImGui, a game
   * view blits it to the swapchain, and an offscreen thumbnail renderer just
   * reads it back. None of that is the Viewport's concern.
   *
   * Viewports are driven in two phases, and the split matters:
   *
   *   1. update(dt, size) -- during input/UI handling. Records the size the
   *      viewport should render at, runs whatever control logic the subclass
   *      has, and prepares the graph (which reallocates images if the size
   *      changed). After this returns, getFinalImage() is valid *at the new
   *      size*.
   *   2. render(renderer)  -- during a centralized render phase, after all UI
   *      has been laid out. Binds this viewport's camera and runs the graph.
   *
   * The reason for the split is the editor case. ImGui::Image() records a quad
   * referencing a VkDescriptorSet, but that quad is not drawn until
   * ImGui::Render() at the very end of the frame. So the image must be
   * allocated (phase 1) before the quad is recorded, while the pixels may be
   * produced any time afterwards (phase 2). Doing the reallocation in phase 1
   * is what keeps the recorded descriptor set from being freed out from under
   * the quad.
   */
  class Viewport {
   public:
    Viewport();
    virtual ~Viewport() = default;

    // Non-copyable, non-movable: it owns a RenderGraph, which is both.
    Viewport(const Viewport &) = delete;
    Viewport &operator=(const Viewport &) = delete;
    Viewport(Viewport &&) = delete;
    Viewport &operator=(Viewport &&) = delete;

    /**
     * @brief Phase 1. Set the size, run control logic, and prepare the graph.
     *
     * Not virtual: the ordering here is the contract. Subclasses customize by
     * overriding onUpdate(), which runs after the size is known but before the
     * graph is prepared, so a controller may react to the viewport's aspect
     * ratio.
     */
    void update(float dt, glm::uvec2 size);

    /**
     * @brief Phase 2. Bind this viewport's frame globals and run its graph.
     */
    void render(Renderer &renderer);

    /**
     * @brief The image this viewport's graph produces.
     * Only valid after update() has run at least once this frame.
     */
    ImageRef getFinalImage() const;

    /**
     * @brief An ImGui-sampleable handle for the final image.
     *
     * Returns VK_NULL_HANDLE before the first update(), since the image does
     * not exist until the graph has been prepared at a real size.
     */
    VkDescriptorSet getImGuiTextureID() const;

    /// Draw this viewport's graph inspector. Debug only.
    void inspect(void);

    glm::uvec2 getRenderSize() const { return renderSize; }

    /// The camera, as plain matrices. Whoever owns the viewport writes these:
    /// an editor controller in onUpdate(), or an ECS system for a game view.
    glm::mat4 view = glm::mat4(1.0f);
    glm::mat4 projection = glm::mat4(1.0f);

   protected:
    /// Subclass hook, called by update() once renderSize is set.
    virtual void onUpdate(float dt) {}

    /// Populate the graph. Called once, from the constructor.
    /// Eventually this is what a RenderPipeline abstraction would replace.
    virtual void buildGraph();

    RenderGraph graph;
    /// The handle the graph's final image lives at. Set by buildGraph().
    GraphHandle finalImage = nullGraphHandle;

   private:
    glm::uvec2 renderSize{1, 1};
    /// False until update() has prepared the graph at least once; the graph's
    /// images do not exist before that.
    bool prepared = false;

    /// The PSO for the placeholder fullscreen pass. Owned here rather than
    /// captured by the graph lambda so its lifetime is obviously the
    /// viewport's.
    PipelineStateObject forwardPSO;
  };


  /**
   * @brief Per-frame mouse state, scoped to one viewport's rectangle.
   *
   * Everything here is derived from an ImGui InvisibleButton covering the
   * viewport, rather than from SDL's global mouse state. That distinction is
   * the whole point: ImGui knows which widget owns the mouse, so two viewports
   * side by side never both react to the same drag, and a popup drawn over a
   * viewport takes the mouse away from it for free.
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
    float moveSpeed = 32.0f;
    /// Radians of rotation per pixel of mouse movement. ren::Camera uses the
    /// same 0.002 against SDL's relative motion, which is also pixels.
    float lookSensitivity = 0.002f;

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

   protected:
    void onUpdate(float dt) override;

   private:
    ViewportInput input;
    /// True while a right-drag look is in progress. Tracked per viewport so a
    /// drag started in one viewport does not rotate another.
    bool looking = false;
  };

}  // namespace ren
