#pragma once

#include <vulkan/vulkan.h>
#include <ren/renderer/CommandEncoder.h>

namespace ren {
  class RenderGraph;
  class RenderTask;
  class Renderer;

  /**
   * @struct GraphRunContext
   * @brief Execution context passed to tasks and resource barriers during schedule execution.
   *
   * This struct carries state needed for recording commands and synchronization during the
   * execution phase. It's designed to be extensible as the render graph system evolves
   * (e.g., adding framebuffer, device references, timing data, etc.).
   */
  struct GraphRunContext {
    ren::RenderGraph &graph;  ///< Reference to the render graph being executed
    ren::RenderTask *task;    ///< Pointer to the task currently being executed
    // TODO: REMOVE
    ren::Renderer &renderer;  ///< Reference to the renderer for render pass operations
    // TODO: REMOVE
    VkCommandBuffer cmd;      ///< Command buffer for recording barriers and commands
    CommandEncoder &encoder;  ///< Command encoder for higher-level command recording

    /**
     * @brief Constructs a GraphRunContext for the given render graph and renderer.
     * @param g Reference to the render graph
     * @param r Reference to the renderer
     */
    GraphRunContext(ren::RenderGraph &g, ren::Renderer &r, CommandEncoder &e)
        : graph(g)
        , task(nullptr)
        , renderer(r)
        , cmd(VK_NULL_HANDLE)
        , encoder(e) {}
  };

  struct GraphRenderPassContext : public GraphRunContext {
    ren::RenderPassEncoder &encoder;


    GraphRenderPassContext(ren::RenderGraph &g, ren::Renderer &r, RenderPassEncoder &e)
        : GraphRunContext(g, r, e.getEncoder())
        , encoder(e) {}
  };

}  // namespace ren
