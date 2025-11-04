#pragma once

#include <ren/renderer/graph/Handle.h>
#include <ren/renderer/graph/Task.h>
#include <ren/renderer/RenderPass.h>

#include <map>

namespace ren {


  /**
   * @brief Represents a single rendering operation to render targets in the graph.
   *
   * This class extends RenderTask functionality by exposing methods for adding color and depth
   * attachments, which are created as image resources within the render graph. These attachments
   * are then automatically constructed and managed by the graph system, and merged into the
   * structure needed for render pass execution.
   *
   *
   * @see RenderTask
   */
  class RenderPassTask : public RenderTask {
   public:
    GraphHandle addColorAttachment(const std::string_view &name, const GraphImageSpec &spec);
    GraphHandle addDepthAttachment(const std::string_view &name, const GraphImageSpec &spec);

   private:
    // We store the render pass description and instance here for execution
    RenderPass::Description desc;
    ref<RenderPass> pass;


    std::map<std::string, GraphHandle> attachmentHandles;
  };

}  // namespace ren