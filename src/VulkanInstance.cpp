#include <SDL2/SDL_vulkan.h>
#include <ren/Vulkan.h>
#include <vector>
#include <fmt/core.h>
#include <fstream>

#include "vkb/VkBootstrap.h"

const int MAX_FRAMES_IN_FLIGHT = 3;


static unsigned int debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                  VkDebugUtilsMessageTypeFlagsEXT messageType,
                                  const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                                  void *pUserData) {
  // ren::VulkanInstance *instance = static_cast<ren::VulkanInstance
  // *>(pUserData);
  auto severity = vkb::to_string_message_severity(messageSeverity);
  auto type = vkb::to_string_message_type(messageType);
  printf("[REN %s: %s] %s\n", severity, type, pCallbackData->pMessage);
  return VK_FALSE;
}

ren::VulkanInstance::VulkanInstance(const std::string &app_name, SDL_Window *window) {
  this->window = window;

  int width, height;
  SDL_Vulkan_GetDrawableSize(window, &width, &height);
  this->extent.width = width;
  this->extent.height = height;

  init_instance();
  init_swapchain();
  init_renderpass();
  init_pipeline();
  init_framebuffers();
  init_command_pool();
  init_command_buffer();
  init_sync_objects();
}


void ren::VulkanInstance::draw_frame(void) {
  // At a high level, rendering a frame in Vulkan consists of a common set of steps:
  //  - Wait for the previous frame to finish
  //  - Acquire an image from the swap chain
  //  - Record a command buffer which draws the scene onto that image
  //  - Submit the recorded command buffer
  //  - Present the swap chain image
  u64 current_frame = (this->frame_number++) % MAX_FRAMES_IN_FLIGHT;

  vkWaitForFences(device, 1, &inFlightFences[current_frame], VK_TRUE, UINT64_MAX);
  vkResetFences(device, 1, &inFlightFences[current_frame]);


  // Acquire the next image from the swapchain
  u32 imageIndex;
  vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphores[current_frame],
                        VK_NULL_HANDLE, &imageIndex);


  vkResetCommandBuffer(commandBuffers[current_frame], 0);
  record_command_buffer(commandBuffers[current_frame], imageIndex);

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

  VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[current_frame]};
  VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = waitSemaphores;
  submitInfo.pWaitDstStageMask = waitStages;

  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffers[current_frame];

  VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[current_frame]};
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSemaphores;

  if (vkQueueSubmit(graphics_queue, 1, &submitInfo, inFlightFences[current_frame]) != VK_SUCCESS) {
    throw std::runtime_error("failed to submit draw command buffer!");
  }

  // Presentation
  VkPresentInfoKHR presentInfo{};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = signalSemaphores;

  VkSwapchainKHR swapChains[] = {swapchain};
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = swapChains;
  presentInfo.pImageIndices = &imageIndex;

  presentInfo.pResults = nullptr;  // Optional

  vkQueuePresentKHR(graphics_queue, &presentInfo);

  current_frame = (current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void ren::VulkanInstance::init_instance(void) {
  vkb::InstanceBuilder builder;
  builder.set_debug_callback([](VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                VkDebugUtilsMessageTypeFlagsEXT messageType,
                                const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                                void *pUserData) -> VkBool32 {
    auto severity = vkb::to_string_message_severity(messageSeverity);
    auto type = vkb::to_string_message_type(messageType);
    printf("[%s: %s] %s\n", severity, type, pCallbackData->pMessage);
    return VK_FALSE;
  });

  // make the vulkan instance, with basic debug features
  auto inst_ret = builder.set_app_name("Example Vulkan Application")
                      .request_validation_layers(true)
                      .require_api_version(1, 1, 0)
                      .build();

  if (!inst_ret) {
    std::cerr << "Failed to create Vulkan instance: " << inst_ret.error() << std::endl;
    exit(-1);
  }

  vkb::Instance vkb_inst = inst_ret.value();

  fmt::print("Vulkan instance created with version: {}.{}.{}\n",
             VK_VERSION_MAJOR(vkb_inst.instance_version),
             VK_VERSION_MINOR(vkb_inst.instance_version),
             VK_VERSION_PATCH(vkb_inst.instance_version));

  // grab the instance and store it away in the VulkanInstance class
  this->instance = vkb_inst.instance;

  // setup a debug messenger
  VkDebugUtilsMessengerCreateInfoEXT messengerCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .pNext = nullptr,
      .flags = 0,
      .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
      .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
      .pfnUserCallback = &debugCallback,
      .pUserData = this,
  };
  PFN_vkCreateDebugUtilsMessengerEXT createDebugUtilsMessenger =
      reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
          vkGetInstanceProcAddr(vkb_inst.instance, "vkCreateDebugUtilsMessengerEXT"));
  if (!createDebugUtilsMessenger) {
    std::cerr << "Failed to load vkCreateDebugUtilsMessengerEXT" << std::endl;
    exit(-1);
  }

  VkResult result = createDebugUtilsMessenger(vkb_inst.instance, &messengerCreateInfo, nullptr,
                                              &this->debug_messenger);
  CHECK_VK_RESULT(result, "Failed to create debug messenger");

  // Create the vulkan surface from SDL
  SDL_Vulkan_CreateSurface(window, instance, &surface);

  // And select the GPU to use (I think we'd need to figure out how to pick the
  // best one if you have multiple GPUs, but I don't so this is fine for now)
  vkb::PhysicalDeviceSelector selector{vkb_inst};
  vkb::PhysicalDevice physicalDevice =
      selector.set_minimum_version(1, 1).set_surface(surface).select().value();
  fmt::print("Selected physical device: {}\n", physicalDevice.name);
  this->physical_device = physicalDevice.physical_device;

  vkb::DeviceBuilder deviceBuilder{physicalDevice};
  vkb::Device vkbDevice = deviceBuilder.build().value();
  this->device = vkbDevice.device;

  this->graphics_queue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
  this->graphics_queue_family = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
  fmt::print("Created graphics queue with family index: {}\n", this->graphics_queue_family);
}

ren::VulkanInstance::~VulkanInstance() {
  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
    vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
    vkDestroyFence(device, inFlightFences[i], nullptr);
  }

  // Command Pool
  vkDestroyCommandPool(device, commandPool, nullptr);


  // Framebuffers
  for (auto framebuffer : swapchain_framebuffers) {
    vkDestroyFramebuffer(device, framebuffer, nullptr);
  }

  // Graphics Pipeline
  vkDestroyPipeline(device, graphics_pipeline, nullptr);
  vkDestroyPipelineLayout(device, pipeline_layout, nullptr);


  vkDestroyRenderPass(device, render_pass, nullptr);

  // Destroy the swapchain image views
  for (auto &image_view : image_views) {
    vkDestroyImageView(device, image_view, nullptr);
  }
  image_views.clear();

  // Destroy the swapchain itself
  vkDestroySwapchainKHR(device, swapchain, nullptr);

  vkDestroySurfaceKHR(instance, surface, nullptr);

  vkDestroyDevice(device, nullptr);

  if (debug_messenger) {
    PFN_vkDestroyDebugUtilsMessengerEXT destroyDebugUtilsMessenger =
        reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (destroyDebugUtilsMessenger) {
      fmt::print("Destroying debug messenger\n");
      destroyDebugUtilsMessenger(instance, debug_messenger, nullptr);
    } else {
      fmt::print("Failed to load vkDestroyDebugUtilsMessengerEXT\n");
    }
  }

  // Cleanup code for the Vulkan instance
  vkDestroyInstance(instance, nullptr);
}


void ren::VulkanInstance::init_swapchain(void) {
  fmt::print("Creating Vulkan swapchain for window size: {}x{}\n", extent.width, extent.height);

  printf("Vulkan instance: %p, device: %p, surface: %p\n", this->instance, this->device,
         this->surface);

  vkb::SwapchainBuilder swapchain_builder(this->physical_device, this->device, this->surface);


  vkb::Swapchain vkb_swapchain =
      swapchain_builder.use_default_format_selection()
          .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)  // Use FIFO present mode
          .set_desired_format(
              {VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})  // SRGB format
          .set_desired_extent(extent.width, extent.height)                   // Window size
          .build()
          .value();

  this->swapchain = vkb_swapchain.swapchain;
  this->images = vkb_swapchain.get_images().value();
  this->image_views = vkb_swapchain.get_image_views().value();
  this->image_format = vkb_swapchain.image_format;
  fmt::print("Vulkan swapchain created with {} images, extent: {}x{}\n", this->images.size(),
             this->extent.width, this->extent.height);
}


void ren::VulkanInstance::init_renderpass(void) {
  // from
  // https://vulkan-tutorial.com/Drawing_a_triangle/Graphics_pipeline_basics/Render_passes
  VkAttachmentDescription colorAttachment{};

  colorAttachment.format = this->image_format;
  colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference colorAttachmentRef{};
  colorAttachmentRef.attachment = 0;
  colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorAttachmentRef;

  VkSubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.srcAccessMask = 0;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;


  VkRenderPassCreateInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.attachmentCount = 1;
  renderPassInfo.pAttachments = &colorAttachment;
  renderPassInfo.subpassCount = 1;
  renderPassInfo.pSubpasses = &subpass;
  //
  renderPassInfo.dependencyCount = 1;
  renderPassInfo.pDependencies = &dependency;

  if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &render_pass) != VK_SUCCESS) {
    throw std::runtime_error("failed to create render pass!");
  }
}



void ren::VulkanInstance::init_pipeline(void) {
  // from https://vulkan-tutorial.com/en/Drawing_a_triangle/Graphics_pipeline_basics/Fixed_functions

  // First, we load the shader modules from the SPIR-V files.
  VkShaderModule vertShaderModule = load_shader_module("shaders/triangle.vert.spv");
  VkShaderModule fragShaderModule = load_shader_module("shaders/triangle.frag.spv");

  // To actually use the shaders we'll need to assign them to a
  // specific pipeline stage through VkPipelineShaderStageCreateInfo
  // structures as part of the actual pipeline creation process.

  VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
  vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertShaderStageInfo.module = vertShaderModule;
  vertShaderStageInfo.pName = "main";


  VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
  fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragShaderStageInfo.module = fragShaderModule;
  fragShaderStageInfo.pName = "main";

  VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};



  // While most of the pipeline state needs to be baked into the
  // pipeline state, a limited amount of the state can actually be
  // changed without recreating the pipeline at draw time. Examples
  // are the size of the viewport, line width and blend constants. If
  // you want to use dynamic state and keep these properties out, then
  // you'll have to fill in a VkPipelineDynamicStateCreateInfo
  // structure like this:
  std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

  VkPipelineDynamicStateCreateInfo dynamicState{};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
  dynamicState.pDynamicStates = dynamicStates.data();

  // This will cause the configuration of these values to be ignored
  // and you will be able (and required) to specify the data at
  // drawing time. This results in a more flexible setup and is very
  // common for things like viewport and scissor state, which would
  // result in a more complex setup when being baked into the pipeline
  // state.



  // Vertex input
  VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
  vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputInfo.vertexBindingDescriptionCount = 0;
  vertexInputInfo.pVertexBindingDescriptions = nullptr;  // Optional
  vertexInputInfo.vertexAttributeDescriptionCount = 0;
  vertexInputInfo.pVertexAttributeDescriptions = nullptr;  // Optional;

  // Input assembly
  VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  inputAssembly.primitiveRestartEnable = VK_FALSE;


  VkPipelineViewportStateCreateInfo viewportState{};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;


  // Rasterizer

  // The rasterizer takes the geometry that is shaped by the vertices
  // from the vertex shader and turns it into fragments to be colored
  // by the fragment shader. It also performs depth testing, face
  // culling and the scissor test, and it can be configured to output
  // fragments that fill entire polygons or just the edges (wireframe
  // rendering). All this is configured using the
  // VkPipelineRasterizationStateCreateInfo structure.
  VkPipelineRasterizationStateCreateInfo rasterizer{};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;  // Discarding fragments is not allowed
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;

  rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

  rasterizer.depthBiasEnable = VK_FALSE;
  rasterizer.depthBiasConstantFactor = 0.0f;  // Optional
  rasterizer.depthBiasClamp = 0.0f;           // Optional
  rasterizer.depthBiasSlopeFactor = 0.0f;     // Optional


  // Multisampling
  VkPipelineMultisampleStateCreateInfo multisampling{};
  multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.sampleShadingEnable = VK_FALSE;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  multisampling.minSampleShading = 1.0f;           // Optional
  multisampling.pSampleMask = nullptr;             // Optional
  multisampling.alphaToCoverageEnable = VK_FALSE;  // Optional
  multisampling.alphaToOneEnable = VK_FALSE;       // Optional


  // Color blending
  // After a fragment shader has returned a color, it needs to be
  // combined with the color that is already in the framebuffer

  VkPipelineColorBlendAttachmentState colorBlendAttachment{};
  colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  colorBlendAttachment.blendEnable = VK_FALSE;
  colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;   // Optional
  colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;  // Optional
  colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;              // Optional
  colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;   // Optional
  colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;  // Optional
  colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;              // Optional

  VkPipelineColorBlendStateCreateInfo colorBlending{};
  colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.logicOp = VK_LOGIC_OP_COPY;  // Optional
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &colorBlendAttachment;
  colorBlending.blendConstants[0] = 0.0f;  // Optional
  colorBlending.blendConstants[1] = 0.0f;  // Optional
  colorBlending.blendConstants[2] = 0.0f;  // Optional
  colorBlending.blendConstants[3] = 0.0f;  // Optional

  // Pipeline layout
  // You can use uniform values in shaders, which are globals similar
  // to dynamic state variables that can be changed at drawing time to
  // alter the behavior of your shaders without having to recreate
  // them. They are commonly used to pass the transformation matrix to
  // the vertex shader, or to create texture samplers in the fragment
  // shader.
  //
  // These uniform values need to be specified during pipeline
  // creation by creating a VkPipelineLayout object.
  //
  // One such uniform value could be the current time, in shadertoy :)


  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 0;             // Optional
  pipelineLayoutInfo.pSetLayouts = nullptr;          // Optional
  pipelineLayoutInfo.pushConstantRangeCount = 0;     // Optional
  pipelineLayoutInfo.pPushConstantRanges = nullptr;  // Optional

  if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipeline_layout) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create pipeline layout!");
  }


  // Now, we can create the graphics pipeline!

  VkGraphicsPipelineCreateInfo pipelineInfo{};
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
  pipelineInfo.layout = pipeline_layout;
  // After that comes the pipeline layout, which is a Vulkan handle rather than a struct pointer.
  pipelineInfo.renderPass = render_pass;
  pipelineInfo.subpass = 0;
  // Required for compat
  pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;  // Optional
  pipelineInfo.basePipelineIndex = -1;               // Optional

  // Finally!
  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                &graphics_pipeline) != VK_SUCCESS) {
    throw std::runtime_error("failed to create graphics pipeline!");
  }

  fmt::print("Graphics pipeline created successfully!\n");
}


void ren::VulkanInstance::init_framebuffers(void) {
  swapchain_framebuffers.resize(image_views.size());

  for (size_t i = 0; i < image_views.size(); i++) {
    VkImageView attachments[] = {image_views[i]};

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = render_pass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = attachments;
    framebufferInfo.width = extent.width;
    framebufferInfo.height = extent.height;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapchain_framebuffers[i]) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to create framebuffer!");
    }
  }
}


void ren::VulkanInstance::init_command_pool(void) {
  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = this->graphics_queue_family;

  if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
    throw std::runtime_error("failed to create command pool!");
  }
}


void ren::VulkanInstance::init_command_buffer(void) {
  commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();

  if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
    throw std::runtime_error("failed to allocate command buffers!");
  }
}


void ren::VulkanInstance::record_command_buffer(VkCommandBuffer commandBuffer, u32 imageIndex) {
  // We always begin recording a command buffer by calling
  // vkBeginCommandBuffer with a small VkCommandBufferBeginInfo
  // structure as argument that specifies some details about the usage
  // of this specific command buffer.

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = 0;                   // Optional
  beginInfo.pInheritanceInfo = nullptr;  // Optional

  if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
    throw std::runtime_error("failed to begin recording command buffer!");
  }

  // Drawing starts by beginning the render pass with
  // vkCmdBeginRenderPass. The render pass is configured using some
  // parameters in a VkRenderPassBeginInfo struct.

  VkRenderPassBeginInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassInfo.renderPass = render_pass;
  renderPassInfo.framebuffer = swapchain_framebuffers[imageIndex];
  renderPassInfo.renderArea.offset = {0, 0};
  renderPassInfo.renderArea.extent = extent;

  // setup the clear values
  VkClearValue clearColor = {{{1.0f, 0.0f, 0.0f, 1.0f}}};
  renderPassInfo.clearValueCount = 1;
  renderPassInfo.pClearValues = &clearColor;

  vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

  // We can now bind the graphics pipeline:
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);


  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = static_cast<float>(extent.width);
  viewport.height = static_cast<float>(extent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

  VkRect2D scissor{};
  scissor.offset = {0, 0};
  scissor.extent = extent;
  vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

  // Now we can issue the draw command for the triangle
  vkCmdDraw(commandBuffer, 3, 1, 0, 0);

  // The render pass can now be ended:
  vkCmdEndRenderPass(commandBuffer);

  // And we've finished recording the command buffer:
  if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
    throw std::runtime_error("failed to record command buffer!");
  }
}



void ren::VulkanInstance::init_sync_objects(void) {
  imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
  renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
  inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

  VkSemaphoreCreateInfo semaphoreInfo{};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VkFenceCreateInfo fenceInfo{};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) !=
            VK_SUCCESS ||
        vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) !=
            VK_SUCCESS ||
        vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {
      throw std::runtime_error("failed to create synchronization objects for a frame!");
    }
  }
}




VkShaderModule ren::VulkanInstance::create_shader_module(const std::vector<u8> &code) {
  VkShaderModuleCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = code.size();
  createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());

  VkShaderModule shaderModule;
  if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
    throw std::runtime_error("failed to create shader module!");
  }

  return shaderModule;
}



VkShaderModule ren::VulkanInstance::load_shader_module(const std::string &filename) {
  std::vector<u8> code;

  // Load the shader code from the file
  std::ifstream file(filename, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    throw std::runtime_error(fmt::format("Failed to open shader file: {}", filename));
  }
  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);
  code.resize(size);
  if (!file.read(reinterpret_cast<char *>(code.data()), size)) {
    throw std::runtime_error(fmt::format("Failed to read shader file: {}", filename));
  }
  file.close();
  fmt::print("Loading shader from {} ({} bytes)\n", filename, size);

  return create_shader_module(code);
}
