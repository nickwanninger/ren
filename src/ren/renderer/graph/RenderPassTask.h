#pragma once

#include <ren/renderer/graph/Handle.h>
#include <ren/renderer/graph/Task.h>
#include <ren/renderer/RenderPass.h>
#include <ren/renderer/RenderTarget.h>

#include <map>

namespace ren {

  /**
   * @brief A render task that manages render pass lifecycle and attachment resources.
   *
   * RenderPassTask extends RenderTask to provide declarative render pass construction. Users:
   * 1. Derive from RenderPassTask
   * 2. Call addColorAttachment() and addDepthAttachment() in the constructor
   * 3. Implement run() with their rendering logic
   *
   * The task automatically:
   * - Creates GraphImageResources for each attachment
   * - Builds a VkRenderPass via caching (deduplication)
   * - Constructs a VkFramebuffer for the current frame
   * - Calls beginPass() before run() and endPass() after (via preRun/postRun)
   *
   * Example:
   * ```cpp
   * class GeometryPass : public RenderPassTask {
   *   GraphHandle colorOut, depthOut;
   *
   *   GeometryPass(RenderGraph& graph) : RenderPassTask(graph) {
   *     colorOut = addColorAttachment("color", {.scale = 1.0f, ...});
   *     depthOut = addDepthAttachment("depth", {.scale = 1.0f, ...});
   *   }
   *
   *   void run(GraphRunContext& ctx) override {
   *     ctx.renderer->bind(pso);
   *     ctx.renderer->draw(...);
   *   }
   * };
   * ```
   *
   * @see RenderTask, RenderPass, RenderTarget
   */
  class RenderPassTask : public RenderTask {
   public:
    RenderPassTask(RenderGraph &graph);
    virtual ~RenderPassTask() = default;

    // Attachment declaration API
    GraphHandle addColorAttachment(const std::string_view &name, const GraphImageSpec &spec);
    GraphHandle addDepthAttachment(const std::string_view &name, const GraphImageSpec &spec);

    // Task lifecycle
    void prepare() override;
    void unprepare() override;

    // Render pass lifecycle hooks
    void preRun(GraphRunContext &ctx) override;
    void postRun(GraphRunContext &ctx) override;

   protected:
    // Derived classes override run() to implement rendering logic.
    // beginPass/endPass are handled automatically by preRun/postRun.
    virtual void run(GraphRunContext &ctx) override = 0;

    // Accessors for derived classes
    ref<RenderPass> getRenderPass() const { return pass; }
    ref<RenderTarget> getRenderTarget() const { return target; }

   private:
    // Render pass description built from attachments
    RenderPass::Description desc;

    // Cached render pass (zero-cost abstraction via deduplication)
    ref<RenderPass> pass;

    // Framebuffer for current frame (rebuilt on attachment resize)
    ref<RenderTarget> target;

    // Maps attachment names to their graph resource handles
    std::map<std::string, GraphHandle> attachmentHandles;
  };



  class RenderPassTaskLambda : public RenderPassTask {
   public:
    RenderPassTaskLambda(RenderGraph &graph, std::function<void(GraphRunContext &ctx)> &&func)
        : RenderPassTask(graph)
        , m_func(std::move(func)) {}

    void run(GraphRunContext &ctx) override { m_func(ctx); }

   private:
    std::function<void(GraphRunContext &ctx)> m_func;
  };

}  // namespace ren