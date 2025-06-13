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



    // ----------- Rendering Usage ------------ //

    inline void bind(VkCommandBuffer commandBuffer) const {
      if (pipeline == VK_NULL_HANDLE) {
        fmt::print("VulkanPipeline::bind() called, but pipeline is not built yet.\n");
        abort();
      }
      vkCmdBindPipeline(commandBuffer, bindPoint, pipeline);
    }


    // ----------- Construction Methods ------------- //


    // Trigger the pipeline to build itself.
    // It will rebuild if needed.
    // This must be called by the *creator* of the pipeline.
    void build(void);


    // Add a shader to the pipeline.
    void addShader(std::shared_ptr<Shader> shader) {
      if (pipeline != VK_NULL_HANDLE) {
        fmt::print("VulkanPipeline::addShader() called, but pipeline is already built.\n");
        abort();
      }
      shaders.push_back(shader);
    }
    // add a binding to the descriptor set layout.
    void addBinding(const VkDescriptorSetLayoutBinding &bindingDesc) {
      if (pipeline != VK_NULL_HANDLE) {
        fmt::print("VulkanPipeline::addBinding() called, but pipeline is already built.\n");
        abort();
      }
      bindings.push_back(bindingDesc);
    }

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

    inline VkDescriptorSetLayout getDescriptorSetLayout(void) const {
      if (descriptorSetLayout == VK_NULL_HANDLE) {
        fmt::print(
            "VulkanPipeline::getDescriptorSetLayout() called, but descriptor set layout is "
            "not built yet.\n");
        abort();
      }
      return descriptorSetLayout;
    }

    inline VkDescriptorPool getDescriptorPool(void) const {
      if (descriptorPool == VK_NULL_HANDLE) {
        fmt::print("VulkanPipeline::getDescriptorPool() called, but it is not built yet.\n");
        abort();
      }
      return descriptorPool;
    }

    inline VkDescriptorSet getDescriptorSet(int imageIndex) const {
      if (imageIndex < 0 || imageIndex >= MAX_FRAMES_IN_FLIGHT) {
        fmt::print("VulkanPipeline::getDescriptorSet() called with invalid index: {}\n",
                   imageIndex);
        abort();
      }
      if (descriptorSets[imageIndex] == VK_NULL_HANDLE) {
        fmt::print(
            "VulkanPipeline::getDescriptorSet() called, but descriptor set for image index "
            "{} is not built yet.\n",
            imageIndex);
        abort();
      }
      return descriptorSets[imageIndex];
    }


    VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    VkPipelineViewportStateCreateInfo viewportState{};
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    VkPipelineMultisampleStateCreateInfo multisampling{};
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    VkPipelineDynamicStateCreateInfo dynamicState{};

    VkGraphicsPipelineCreateInfo pipelineInfo{};

   protected:
    VulkanInstance &vulkan;

    std::vector<std::shared_ptr<Shader>> shaders;
    std::vector<VkDescriptorSetLayoutBinding> bindings;

    // The pipeline layout
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    // The main Vulkan pipeline handle.
    VkPipeline pipeline = VK_NULL_HANDLE;
    // The pipeline layout.
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    // A pipeline has its own descriptor pool.
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    // Descriptor Sets are per swapchain image.
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> descriptorSets{0};


   private:
    void cleanup(void);
    // Populate the default create info below for this pipeline.
    void populateDefaultCreateInfo();
  };
};  // namespace ren
