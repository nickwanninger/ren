#pragma once

#include <ren/types.h>
#include <ren/Shader.h>
#include <vector>

namespace ren {


  class VulkanInstance;

  // This is the base class for all kinds of Vulkan Pipelines.
  // For example, you might have a GraphicsPipeline, ComputePipeline, etc.
  class VulkanPipeline {
   public:
    VulkanPipeline(VulkanInstance &vulkan);
    virtual ~VulkanPipeline();

    VulkanPipeline(const VulkanPipeline &) = delete;
    VulkanPipeline &operator=(const VulkanPipeline &) = delete;
    VulkanPipeline(VulkanPipeline &&) = delete;
    VulkanPipeline &operator=(VulkanPipeline &&) = delete;


    // Trigger the pipeline to build itself.
    // It will rebuild if needed.
    // This must be called by the *creator* of the pipeline.
    void build(void);

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

    // Add a shader to the pipeline.
    void addShader(std::shared_ptr<Shader> shader) { shaders.push_back(shader); }

   protected:
    VulkanInstance &vulkan;

    std::vector<std::shared_ptr<Shader>> shaders;

    // The pipeline layout
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    // The main Vulkan pipeline handle.
    VkPipeline pipeline = VK_NULL_HANDLE;


   private:
    void cleanup(void);
    // Populate the default create info below for this pipeline.
    void populateDefaultCreateInfo();
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    VkPipelineViewportStateCreateInfo viewportState{};
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    VkPipelineMultisampleStateCreateInfo multisampling{};
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    VkPipelineDynamicStateCreateInfo dynamicState{};

    VkGraphicsPipelineCreateInfo pipelineInfo{};
  };
};  // namespace ren
