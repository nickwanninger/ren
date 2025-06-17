#pragma once

#include <ren/types.h>
#include <ren/renderer/Shader.h>
#include <vector>

namespace ren {


  class VulkanInstance;

  // This is the base class for all Vulkan pipelines.
  // It is intentionally designed to be generic, and requires
  // subclasses actually construct the pipeline.
  class VulkanPipeline {
   public:
    VulkanPipeline() = default;
    virtual ~VulkanPipeline();

    VulkanPipeline(const VulkanPipeline &) = delete;
    VulkanPipeline &operator=(const VulkanPipeline &) = delete;
    VulkanPipeline(VulkanPipeline &&) = delete;
    VulkanPipeline &operator=(VulkanPipeline &&) = delete;



    // ----------- Rendering Usage ------------ //

    inline void bind(VkCommandBuffer commandBuffer) const {
      if (pipeline == VK_NULL_HANDLE) {
        fmt::print("VulkanPipeline::bind() called, but pipeline is not built yet.\n");
        abort();
      }
      vkCmdBindPipeline(commandBuffer, bindPoint, pipeline);
    }


    // ----------- Construction Methods ------------- //

    inline VkPipeline getHandle(void) const {
      if (pipeline == VK_NULL_HANDLE) {
        fmt::print("VulkanPipeline::getHandle() called, but pipeline is not built yet.\n");
        abort();
      }
      return pipeline;
    }

    inline VkPipelineLayout getLayout(void) const {
      if (pipelineLayout == VK_NULL_HANDLE) {
        fmt::print("VulkanPipeline::getLayout() called, but pipeline layout is not built yet.\n");
        abort();
      }
      return pipelineLayout;
    }

   protected:
    // Update this in subclasses to set the bind point.
    VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    // The pipeline layout
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    // The main Vulkan pipeline handle.
    VkPipeline pipeline = VK_NULL_HANDLE;

   private:
    void cleanup(void);
    // Populate the default create info below for this pipeline.
    void populateDefaultCreateInfo();
  };

  inline void bind(VkCommandBuffer commandBuffer, const VulkanPipeline &pipeline) {
    pipeline.bind(commandBuffer);
  }
};  // namespace ren
