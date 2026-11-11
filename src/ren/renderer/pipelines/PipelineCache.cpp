#include <ren/renderer/pipelines/PipelineCache.h>
#include <ren/misc/hash.h>
#include <ren/renderer/vulkan/Vulkan.h>
#include <ren/renderer/shader/ShaderProgram.h>
#include <fstream>
#include <cstring>


namespace ren {

  PipelineCache::PipelineCache() {
    auto &vulkan = ren::getVulkan();

    VkPipelineCacheCreateInfo cacheInfo{};
    cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    cacheInfo.initialDataSize = 0;
    cacheInfo.pInitialData = nullptr;

    if (vkCreatePipelineCache(vulkan.device, &cacheInfo, nullptr, &vkCache) != VK_SUCCESS) {
      throw std::runtime_error("failed to create pipeline cache!");
    }
  }

  PipelineCache::~PipelineCache() {
    if (vkCache != VK_NULL_HANDLE) {
      auto &vulkan = ren::getVulkan();
      vkDestroyPipelineCache(vulkan.device, vkCache, nullptr);
      vkCache = VK_NULL_HANDLE;
    }
  }

  ref<CachedPipeline> PipelineCache::get(ren::RenderPass &renderPass,
                                         const PipelineStateObject &pso) {
    u64 hash = pso.hash();
    ren::hash(hash, reinterpret_cast<u64>(renderPass.getHandle()));

    // Check if we already have this pipeline in the cache.
    auto it = pipelines.find(hash);
    if (it != pipelines.end()) {
      // If we found it, return the cached pipeline.
      return it->second;
    }

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
    if (not pso.hasVertexBinding) {
      // If the PSO does not have a vertex binding, we set the binding count to 0.
      vertexInputInfo.vertexBindingDescriptionCount = 0;
      vertexInputInfo.pVertexBindingDescriptions = nullptr;
      vertexInputInfo.vertexAttributeDescriptionCount = 0;
      vertexInputInfo.pVertexAttributeDescriptions = 0;
    } else {
      vertexInputInfo.vertexBindingDescriptionCount = 1;
      vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
      vertexInputInfo.vertexAttributeDescriptionCount = attributeDescs.size();
      vertexInputInfo.pVertexAttributeDescriptions = attributeDescs.data();
    }


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
    rasterizer.frontFace =
        pso.frontCCW ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
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
    // Pick the max samples used by this render pass's attachments (handles MSAA pipelines)
    VkSampleCountFlagBits passSamples = VK_SAMPLE_COUNT_1_BIT;
    for (const auto &att : renderPass.getDescription().attachments) {
      if (att.samples > passSamples) passSamples = att.samples;
    }
    multisampling.rasterizationSamples = passSamples;
    multisampling.minSampleShading = 1.0f;           // Optional
    multisampling.pSampleMask = nullptr;             // Optional
    multisampling.alphaToCoverageEnable = VK_FALSE;  // Optional
    multisampling.alphaToOneEnable = VK_FALSE;       // Optional



    // Determine the effective number of color attachments used by the subpass.
    // If there are multisampled color attachments, single-sample colors are used as resolves.
    int colorAttachmentCount = 0;
    bool hasMsaaColor = false;
    for (const auto &att : renderPass.getDescription().attachments) {
      if (att.finalLayout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL &&
          att.samples != VK_SAMPLE_COUNT_1_BIT) {
        hasMsaaColor = true;
        break;
      }
    }
    for (const auto &att : renderPass.getDescription().attachments) {
      if (att.finalLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) continue;
      if (hasMsaaColor) {
        if (att.samples != VK_SAMPLE_COUNT_1_BIT) colorAttachmentCount++;
      } else {
        colorAttachmentCount++;
      }
    }
    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments;

    for (int i = 0; i < colorAttachmentCount; ++i) {
      // ---- Color Blend Attachment State ---- //
      VkPipelineColorBlendAttachmentState colorBlendAttachment{};
      colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;


      if (pso.blendMode == BlendMode::None) {
        colorBlendAttachment.blendEnable = VK_FALSE;
      } else {
        colorBlendAttachment.blendEnable = VK_TRUE;

        switch (pso.blendMode) {
          case BlendMode::Alpha:
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            break;
          case BlendMode::Additive:
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            break;
          case BlendMode::Subtractive:
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_SUBTRACT;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_SUBTRACT;
            break;
          case BlendMode::Multiplicative:
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            break;
          default:
            // Default to no blending
            colorBlendAttachment.blendEnable = VK_FALSE;
            break;
        }
      }
      colorBlendAttachments.push_back(colorBlendAttachment);
    }


    // ---- Color Blending Create Info ---- //
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
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
    std::vector<ref<ShaderModule>> shaders = pso.program->getShaders();

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


    if (vkCreateGraphicsPipelines(vulkan.device, vkCache, 1, &pipelineInfo, nullptr,
                                  &pipeline) != VK_SUCCESS) {
      throw std::runtime_error("failed to create graphics pipeline!");
    }

    auto cachedPipeline = make<CachedPipeline>(pipeline, pso);
    pipelines[hash] = cachedPipeline;
    return cachedPipeline;
  }

  void PipelineCache::save(std::string_view filename) const {
    auto &vulkan = ren::getVulkan();

    // Get the size of the cache data
    size_t dataSize = 0;
    if (vkGetPipelineCacheData(vulkan.device, vkCache, &dataSize, nullptr) != VK_SUCCESS) {
      throw std::runtime_error("failed to get pipeline cache data size!");
    }

    // Allocate buffer and retrieve the data
    std::vector<uint8_t> cacheData(dataSize);
    if (vkGetPipelineCacheData(vulkan.device, vkCache, &dataSize, cacheData.data()) != VK_SUCCESS) {
      throw std::runtime_error("failed to get pipeline cache data!");
    }

    // Write to file
    std::ofstream file(filename.data(), std::ios::binary);
    if (!file.is_open()) {
      throw std::runtime_error(std::string("failed to open pipeline cache file for writing: ") +
                                filename.data());
    }

    file.write(reinterpret_cast<const char *>(cacheData.data()), dataSize);
    if (!file.good()) {
      throw std::runtime_error("failed to write pipeline cache data to file!");
    }
  }

  void PipelineCache::load(std::string_view filename) {
    auto &vulkan = ren::getVulkan();

    // Read from file
    std::ifstream file(filename.data(), std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
      // File doesn't exist, that's okay - we'll just start with an empty cache
      return;
    }

    // Get file size
    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    // Read data
    std::vector<uint8_t> cacheData(fileSize);
    if (!file.read(reinterpret_cast<char *>(cacheData.data()), fileSize)) {
      throw std::runtime_error("failed to read pipeline cache data from file!");
    }

    // Validate the cache UUID against the physical device
    // Pipeline cache data format: header (4 bytes version + 4 bytes vendor + 4 bytes device + 16 bytes UUID)
    // Total header = 28 bytes. If smaller, it's invalid.
    if (cacheData.size() < 28) {
      // Cache file is too small, discard it
      return;
    }

    // Get the physical device properties to compare UUIDs
    VkPhysicalDeviceProperties deviceProps{};
    vkGetPhysicalDeviceProperties(vulkan.physical_device, &deviceProps);

    // Extract UUID from cache data (at offset 12, 16 bytes)
    const uint8_t *cachedUUID = cacheData.data() + 12;

    // Compare UUIDs (16 bytes)
    if (std::memcmp(cachedUUID, deviceProps.pipelineCacheUUID, VK_UUID_SIZE) != 0) {
      // UUID mismatch - cache is not compatible with this device, start fresh
      return;
    }

    // Destroy the old cache
    if (vkCache != VK_NULL_HANDLE) {
      vkDestroyPipelineCache(vulkan.device, vkCache, nullptr);
    }

    // Create new cache with the loaded data
    VkPipelineCacheCreateInfo cacheInfo{};
    cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    cacheInfo.initialDataSize = cacheData.size();
    cacheInfo.pInitialData = cacheData.data();

    if (vkCreatePipelineCache(vulkan.device, &cacheInfo, nullptr, &vkCache) != VK_SUCCESS) {
      throw std::runtime_error("failed to create pipeline cache from loaded data!");
    }
  }

  ref<CachedPipeline> PipelineCache::getCompute(ref<ShaderProgram> program) {
    auto& vulkan = ren::getVulkan();
    // Hash the program pointer
    u64 hash = std::hash<void*>{}(program.get());

    if (pipelines.count(hash)) {
      return pipelines[hash];
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.layout = program->getPipelineLayout();

    auto shaders = program->getShaders();
    REN_ASSERT(shaders.size() == 1); // Compute should have 1 shader

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaders[0]->getHandle();
    stageInfo.pName = "main";

    pipelineInfo.stage = stageInfo;

    VkPipeline pipeline;
    if (vkCreateComputePipelines(vulkan.device, vkCache, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
      throw std::runtime_error("failed to create compute pipeline!");
    }

    // Create a dummy PSO for the cached pipeline
    PipelineStateObject dummyPso;
    dummyPso.program = program;

    auto cached = make<CachedPipeline>(pipeline, dummyPso);
    pipelines[hash] = cached;
    return cached;
  }

}  // namespace ren
