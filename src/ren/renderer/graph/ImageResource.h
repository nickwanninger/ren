#pragma once

#include <ren/renderer/Image.h>
#include <ren/renderer/graph/Resource.h>
#include <ren/renderer/Sampler.h>

namespace ren {

  /**
   * @brief A graph resource that wraps an Image.
   * Used internally by RenderGraph to manage image resources and handle
   * image-specific synchronization barriers.
   */
  struct ImageResource : public GraphResource {
    /**
     * @brief Construct an ImageResource with the given specification.
     * The image itself is not allocated until update() is called.
     */
    ImageResource(const GraphImageSpec &spec);
    virtual ~ImageResource() = default;

    /**
     * @brief Update the image resource based on swapchain size.
     * Allocates or reallocates the image if necessary.
     * Only rebuilds swapchain-relative images when the swapchain size changes.
     * Fixed-size images are only built once (when null).
     * @return true if the image was allocated or rebuilt, false if no changes
     */
    bool update(ren::RenderGraph &G) override;

    void emitBarrier(GraphRunContext &ctx, GraphAccess fromAccess, GraphAccess toAccess) override;

    void inspect() const override;

    /**
     * @brief Build or rebuild the VkImage resource.
     * @param swapchainSize The current swapchain dimensions
     */
    void buildImage(glm::uvec2 swapchainSize);

    GraphImageSpec spec;
    ren::ImageRef image;

    ren::Sampler sampler;

    mutable VkDescriptorSet imguiTextureID = VK_NULL_HANDLE;
  };
}  // namespace ren