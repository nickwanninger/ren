#pragma once

#include <ren/renderer/graph/Handle.h>
#include <ren/renderer/graph/Task.h>
#include <ren/renderer/RenderPass.h>
#include <ren/renderer/RenderTarget.h>

#include "ren/renderer/graph/RunContext.h"

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
   * - Calls run() within a render pass context
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

    virtual void run(GraphRenderPassContext &ctx) = 0;

   protected:
    // Derived classes override run() to implement rendering logic.
    // beginPass/endPass are handled automatically by preRun/postRun.
    virtual void run(GraphRunContext &ctx) override final;

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
    std::vector<std::pair<std::string, GraphHandle>> attachmentHandles;
  };

}  // namespace ren