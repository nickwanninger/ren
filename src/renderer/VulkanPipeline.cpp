#include <ren/VulkanPipeline.h>
#include <ren/Vulkan.h>
#include "vulkan/vulkan_core.h"


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
  if (descriptorSetLayout != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(vulkan.device, descriptorSetLayout, nullptr);
    pipeline = VK_NULL_HANDLE;
  }

  if (descriptorPool != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(vulkan.device, descriptorPool, nullptr);
    pipeline = VK_NULL_HANDLE;
  }

  if (pipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(vulkan.device, pipeline, nullptr);
    pipeline = VK_NULL_HANDLE;
  }

  if (pipelineLayout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(vulkan.device, pipelineLayout, nullptr);
    pipelineLayout = VK_NULL_HANDLE;
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


  // Depth stencil Create Info
  depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable = VK_TRUE;
  depthStencil.depthWriteEnable = VK_TRUE;
  depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
  depthStencil.depthBoundsTestEnable = VK_FALSE;
  depthStencil.minDepthBounds = 0.0f;  // Optional
  depthStencil.maxDepthBounds = 1.0f;  // Optional
  depthStencil.stencilTestEnable = VK_FALSE;
  depthStencil.front = {};  // Optional
  depthStencil.back = {};   // Optional


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
  // pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;  // TODO: Not sure
  pipelineLayoutInfo.pushConstantRangeCount = 0;     // Optional
  pipelineLayoutInfo.pPushConstantRanges = nullptr;  // Optional



  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = 2;
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
  cleanup();

  // ---- Descriptor Set Layout ---- //

  VkDescriptorSetLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = static_cast<uint32_t>(this->bindings.size());
  layoutInfo.pBindings = bindings.data();

  if (vkCreateDescriptorSetLayout(vulkan.device, &layoutInfo, nullptr,
                                  &this->descriptorSetLayout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create descriptor set layout!");
  }
  // ---- Descriptor Pool ---- ///

  // Make the descriptor pool
  std::vector<VkDescriptorPoolSize> poolSizes;
  for (auto &binding : bindings) {
    VkDescriptorPoolSize ps;
    ps.descriptorCount = MAX_FRAMES_IN_FLIGHT;
    ps.type = binding.descriptorType;
    poolSizes.push_back(ps);
  }

  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  poolInfo.pPoolSizes = poolSizes.data();
  poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

  if (vkCreateDescriptorPool(vulkan.device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
    throw std::runtime_error("failed to create descriptor pool!");
  }


  // ---- Descriptor Sets ---- //

  std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT,
                                             getDescriptorSetLayout());
  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = descriptorPool;
  allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
  allocInfo.pSetLayouts = layouts.data();

  if (vkAllocateDescriptorSets(vulkan.device, &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
    throw std::runtime_error("failed to allocate descriptor sets!");
  }

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = vulkan.uniform_buffers[i]->getHandle();
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(UniformBufferObject);

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    // TODO: texture here!
    imageInfo.imageView = vulkan.textureImageView;
    imageInfo.sampler = vulkan.textureSampler;


    std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = descriptorSets[i];
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &bufferInfo;

    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = descriptorSets[i];
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pImageInfo = &imageInfo;



    vkUpdateDescriptorSets(vulkan.device, static_cast<uint32_t>(descriptorWrites.size()),
                           descriptorWrites.data(), 0, nullptr);
  }


  // ---- Pipeline Layout Info ---- //


  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &this->descriptorSetLayout;
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
  pipelineInfo.pDepthStencilState = &depthStencil;


  pipelineInfo.pDynamicState = &dynamicState;
  // Then we reference all of the structures describing the fixed-function stage.
  pipelineInfo.layout = pipelineLayout;

  if (vkCreateGraphicsPipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                &this->pipeline) != VK_SUCCESS) {
    throw std::runtime_error("failed to create graphics pipeline!");
  }
}
