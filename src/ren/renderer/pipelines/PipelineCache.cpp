#include <ren/renderer/pipelines/PipelineCache.h>
#include <ren/misc/hash.h>
#include <ren/renderer/Vulkan.h>
#include <ren/renderer/ShaderProgram.h>


namespace ren {



  ref<CachedPipeline> PipelineCache::get(ren::RenderPass &renderPass,
                                         const PipelineStateObject &pso) {
    u64 hash = pso.hash();

    // Check if we already have this pipeline in the cache.
    auto it = pipelines.find(hash);
    if (it != pipelines.end()) {
      // If we found it, return the cached pipeline.
      return it->second;
    }


    fmt::println("Creating new pipeline for render pass '{}'", renderPass.getName());


    // Otherwise, we gotta create a new pipeline!

    // Grab a reference to the Vulkan instance.
    auto &vulkan = ren::getVulkan();
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    // We're gonna convert the abstractions of the PipelineStateObject into the
    // vulkan specifics needed to create a VkPipeline.


    // TODO: add this to the PSO instead.
    auto bindingDesc = ren::Vertex::getBindingDesc();
    auto attributeDescs = ren::Vertex::getAttrDescs();
    // ---- Vertex Input Create Info ---- //
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = attributeDescs.size();
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescs.data();


    // ---- Input Assembly Create Info ---- //
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    // Pull the topology from the PSO.
    switch (pso.topology) {
      case Topology::TriangleList:
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        break;
      case Topology::LineList: inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; break;
    }
    inputAssembly.primitiveRestartEnable = VK_FALSE;


    // ---- Viewport State Create Info ---- //
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;




    // ---- Rasterizer Create Info ---- //
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;  // Discarding fragments is not allowed
    switch (pso.fillMode) {
      case FillMode::Solid: rasterizer.polygonMode = VK_POLYGON_MODE_FILL; break;
      case FillMode::Wireframe: rasterizer.polygonMode = VK_POLYGON_MODE_LINE; break;
    }
    rasterizer.lineWidth = 1.0f;
    switch (pso.cullMode) {
      case CullMode::None: rasterizer.cullMode = VK_CULL_MODE_NONE; break;
      case CullMode::Back: rasterizer.cullMode = VK_CULL_MODE_BACK_BIT; break;
      case CullMode::Front: rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT; break;
    }
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = pso.depthBias != 0.0f;      // Enable depth bias if it's non-zero.
    rasterizer.depthBiasConstantFactor = pso.depthBias;      // Optional
    rasterizer.depthBiasClamp = pso.depthBiasClamp;          // Optional
    rasterizer.depthBiasSlopeFactor = pso.depthSlopeFactor;  // Optional



    // ---- Depth Stencil State Create Info ---- //
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = pso.depthTest;
    depthStencil.depthWriteEnable = pso.depthWrite;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.minDepthBounds = 0.0f;  // Optional
    depthStencil.maxDepthBounds = 1.0f;  // Optional
    depthStencil.stencilTestEnable = VK_FALSE;
    depthStencil.front = {};  // Optional
    depthStencil.back = {};   // Optional


    // ---- Multisample State Create Info ---- //
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading = 1.0f;           // Optional
    multisampling.pSampleMask = nullptr;             // Optional
    multisampling.alphaToCoverageEnable = VK_FALSE;  // Optional
    multisampling.alphaToOneEnable = VK_FALSE;       // Optional



    int colorAttachmentCount = renderPass.getDescription().colorAttachments;
    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments;

    for (int i = 0; i < colorAttachmentCount; ++i) {
      // ---- Color Blend Attachment State ---- //
      VkPipelineColorBlendAttachmentState colorBlendAttachment{};
      colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;


      if (pso.blendMode == BlendMode::None) {
        colorBlendAttachment.blendEnable = VK_FALSE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;   // Optional
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;  // Optional
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;              // Optional
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;   // Optional
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;  // Optional
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;              // Optional
      } else {
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;   // Optional
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;  // Optional
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;              // Optional
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;   // Optional
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;  // Optional
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;              // Optional

        switch (pso.blendMode) {
          case BlendMode::Alpha:
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;                   // Optional
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;  // Optional
            colorBlendAttachment.dstAlphaBlendFactor =
                VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;  // Optional
            break;
          case BlendMode::Additive:
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;             // Optional
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;  // Optional
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;  // Optional
            break;
          case BlendMode::Subtractive:
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_SUBTRACT;         // Optional
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;   // Optional
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;  // Optional
            break;
          case BlendMode::Multiplicative:
            // ??
            // colorBlendAttachment.colorBlendOp = VK_BLEND_OP_MULTIPLY;  // Optional
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_DST_COLOR;  // Optional
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;       // Optional
            break;
          default: break;
        }
      }
      colorBlendAttachments.push_back(colorBlendAttachment);
    }


    // ---- Color Blending Create Info ---- //
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_TRUE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;  // Optional
    colorBlending.attachmentCount = colorAttachmentCount;
    colorBlending.pAttachments = colorBlendAttachments.data();
    colorBlending.blendConstants[0] = 0.0f;  // Optional
    colorBlending.blendConstants[1] = 0.0f;  // Optional
    colorBlending.blendConstants[2] = 0.0f;  // Optional
    colorBlending.blendConstants[3] = 0.0f;  // Optional


    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                                                 VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();


    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    std::vector<ref<Shader>> shaders = pso.program->getShaders();

    for (auto &shader : shaders) {
      VkPipelineShaderStageCreateInfo stageInfo{};
      stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      stageInfo.stage = shader->getStage();
      stageInfo.module = shader->getHandle();
      stageInfo.pName = "main";

      shaderStages.push_back(stageInfo);
    }


    VkGraphicsPipelineCreateInfo pipelineInfo{};
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
    pipelineInfo.layout = pso.program->getPipelineLayout();
    // After that comes the pipeline layout, which is a Vulkan handle rather than a struct pointer.
    pipelineInfo.renderPass = renderPass.getHandle();
    pipelineInfo.subpass = 0;
    // Required for compat
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;  // Optional
    pipelineInfo.basePipelineIndex = -1;               // Optional

    pipelineInfo.stageCount = shaderStages.size();
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pDepthStencilState = &depthStencil;


    if (vkCreateGraphicsPipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                  &pipeline) != VK_SUCCESS) {
      throw std::runtime_error("failed to create graphics pipeline!");
    }

    auto cachedPipeline = makeRef<CachedPipeline>(pipeline, pso);
    pipelines[hash] = cachedPipeline;
    return cachedPipeline;
  }

}  // namespace ren