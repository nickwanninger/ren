#include <ren/renderer/pipelines/VulkanPipeline.h>
#include <ren/renderer/Vulkan.h>
#include "vulkan/vulkan_core.h"



ren::VulkanPipeline::~VulkanPipeline() { cleanup(); }

void ren::VulkanPipeline::cleanup(void) {
  auto &vulkan = ren::getVulkan();

  if (pipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(vulkan.device, pipeline, nullptr);
    pipeline = VK_NULL_HANDLE;
  }

  if (pipelineLayout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(vulkan.device, pipelineLayout, nullptr);
    pipelineLayout = VK_NULL_HANDLE;
  }
}