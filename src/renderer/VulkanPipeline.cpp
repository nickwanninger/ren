#include <ren/VulkanPipeline.h>
#include <ren/Vulkan.h>


ren::VulkanPipeline::VulkanPipeline(ren::VulkanInstance &vulkan)
    : vulkan(vulkan) {
  populateDefaultCreateInfo();
}

ren::VulkanPipeline::~VulkanPipeline() {
  cleanup();
  // Cleanup code for the Vulkan pipeline
  // This will be overridden by derived classes to clean up specific resources
}

void ren::VulkanPipeline::cleanup(void) {
  if (pipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(vulkan.device, pipeline, nullptr);
    pipeline = VK_NULL_HANDLE;
  }

  if (pipeline_layout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(vulkan.device, pipeline_layout, nullptr);
    pipeline_layout = VK_NULL_HANDLE;
  }
}

#define POISON_PTR ((void *)-1UUL)
#define POISON_VAL (-1UUL)

void ren::VulkanPipeline::populateDefaultCreateInfo() {
  // Vertex Input Create Info
  vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputInfo.vertexBindingDescriptionCount = 1;
  vertexInputInfo.vertexAttributeDescriptionCount = 0;     // REQUIRED;
  vertexInputInfo.pVertexBindingDescriptions = nullptr;    // REQUIRED
  vertexInputInfo.pVertexAttributeDescriptions = nullptr;  // REQUIRED

  // Input Assembly Create Info
  inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  inputAssembly.primitiveRestartEnable = VK_FALSE;

  // Viewport State Create Info
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;


  // Rasterizer Create Info
  // The rasterizer takes the geometry that is shaped by the vertices
  // from the vertex shader and turns it into fragments to be colored
  // by the fragment shader. It also performs depth testing, face
  // culling and the scissor test, and it can be configured to output
  // fragments that fill entire polygons or just the edges (wireframe
  // rendering). All this is configured using the
  // VkPipelineRasterizationStateCreateInfo structure.
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;  // Discarding fragments is not allowed
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;

  rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

  rasterizer.depthBiasEnable = VK_FALSE;
  rasterizer.depthBiasConstantFactor = 0.0f;  // Optional
  rasterizer.depthBiasClamp = 0.0f;           // Optional
  rasterizer.depthBiasSlopeFactor = 0.0f;     // Optional


  // Multisample Create Info
  multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.sampleShadingEnable = VK_FALSE;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  multisampling.minSampleShading = 1.0f;           // Optional
  multisampling.pSampleMask = nullptr;             // Optional
  multisampling.alphaToCoverageEnable = VK_FALSE;  // Optional
  multisampling.alphaToOneEnable = VK_FALSE;       // Optional



  // Color blending Create Info
  // After a fragment shader has returned a color, it needs to be
  // combined with the color that is already in the framebuffer
  colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  colorBlendAttachment.blendEnable = VK_FALSE;
  colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;   // Optional
  colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;  // Optional
  colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;              // Optional
  colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;   // Optional
  colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;  // Optional
  colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;              // Optional


  colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.logicOp = VK_LOGIC_OP_COPY;  // Optional
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &colorBlendAttachment;
  colorBlending.blendConstants[0] = 0.0f;  // Optional
  colorBlending.blendConstants[1] = 0.0f;  // Optional
  colorBlending.blendConstants[2] = 0.0f;  // Optional
  colorBlending.blendConstants[3] = 0.0f;  // Optional



  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &vulkan.descriptorSetLayout;  // TODO: Not sure
  pipelineLayoutInfo.pushConstantRangeCount = 0;                 // Optional
  pipelineLayoutInfo.pPushConstantRanges = nullptr;              // Optional



  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = 2;
  pipelineInfo.pStages = shaderStages;
  // We start by referencing the array of VkPipelineShaderStageCreateInfo structs.
  pipelineInfo.pVertexInputState = &vertexInputInfo;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pRasterizationState = &rasterizer;
  pipelineInfo.pMultisampleState = &multisampling;
  pipelineInfo.pDepthStencilState = nullptr;  // Optional
  pipelineInfo.pColorBlendState = &colorBlending;
  pipelineInfo.pDynamicState = &dynamicState;
  // Then we reference all of the structures describing the fixed-function stage.
  pipelineInfo.layout = pipelineLayout;
  // After that comes the pipeline layout, which is a Vulkan handle rather than a struct pointer.
  pipelineInfo.renderPass = vulkan.render_pass;
  pipelineInfo.subpass = 0;
  // Required for compat
  pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;  // Optional
  pipelineInfo.basePipelineIndex = -1;               // Optional
}


void ren::VulkanPipeline::build(void) {
  // First, build the pipeline layout.

  cleanup();


  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &vulkan.descriptorSetLayout;
  pipelineLayoutInfo.pushConstantRangeCount = 0;     // Optional
  pipelineLayoutInfo.pPushConstantRanges = nullptr;  // Optional

  if (vkCreatePipelineLayout(vulkan.device, &pipelineLayoutInfo, nullptr, &this->pipelineLayout) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create pipeline layout!");
  }



  std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamicState{};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
  dynamicState.pDynamicStates = dynamicStates.data();


  std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

  for (auto &shader : this->shaders) {
    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = shader->getStage();
    stageInfo.module = shader->getHandle();
    stageInfo.pName = "main";

    shaderStages.push_back(stageInfo);
  }

  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  // Build the pipeline stages
  pipelineInfo.stageCount = shaderStages.size();
  pipelineInfo.pStages = shaderStages.data();

  pipelineInfo.pDynamicState = &dynamicState;
  // Then we reference all of the structures describing the fixed-function stage.
  pipelineInfo.layout = pipelineLayout;

  if (vkCreateGraphicsPipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                &this->pipeline) != VK_SUCCESS) {
    throw std::runtime_error("failed to create graphics pipeline!");
  }
}
