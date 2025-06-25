#pragma once

#include <vector>

#include <ren/renderer/Shader.h>

namespace ren {

  struct PipelineStateDesc {
    // The stages needed for this pipeline.
    std::vector<ref<Shader>> shaderStages;

    // Arguments to VkPipelineVertexInputStateCreateInfo.
    std::vector<VkVertexInputBindingDescription> vertexInputBindings;
    std::vector<VkVertexInputAttributeDescription> vertexInputAttributes;

    // Options for Input Assembly.
    // Default to triangle list for simplicity.
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkBool32 primitiveRestartEnable = VK_FALSE;

    // Viewport/scissor (if not dynamic)
    std::vector<VkViewport> viewports;
    std::vector<VkRect2D> scissors;


    // Rasterization info.
    VkPipelineRasterizationStateCreateInfo rasterizationState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp = 0.0f,
        .depthBiasSlopeFactor = 0.0f,
        .lineWidth = 1.0f};
  };
}  // namespace ren