#include <SDL2/SDL_vulkan.h>
#include <ren/Vulkan.h>
#include <vector>
#include <fmt/core.h>
#include <fstream>
#include "imgui.h"
#include "vkb/VkBootstrap.h"
#include "vulkan/vulkan_core.h"

#include <stb/stb_image.h>


#include <imconfig.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imstb_rectpack.h>
#include <imstb_textedit.h>
#include <imstb_truetype.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_vulkan.h>


// Implement vk_mem_alloc here!
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

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

  create_descriptor_set_layout();


  init_pipeline();
  init_framebuffers();
  init_command_pool();

  createTextureImage();
  createVertexBuffer();
  create_descriptor_pool();
  create_descriptor_sets();


  init_command_buffer();
  init_sync_objects();


  init_imgui();
}



std::vector<ren::Vertex> vertices = {
    //
    {
        glm::vec3(-0.5f, -0.5f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        -glm::vec2(1.0f, 0.0f),
    },
    {
        glm::vec3(0.5f, -0.5f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        -glm::vec2(0.0f, 0.0f),
    },
    {
        glm::vec3(0.5f, 0.5f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        -glm::vec2(0.0f, 1.0f),
    },
    {
        glm::vec3(-0.5f, 0.5f, 0.0f),
        glm::vec3(1.0f, 1.0f, 1.0f),
        -glm::vec2(1.0f, 1.0f),
    }
    //
};

const std::vector<uint32_t> indices = {0, 1, 2, 2, 3, 0};

void ren::VulkanInstance::draw_frame(void) {
  // At a high level, rendering a frame in Vulkan consists of a common set of steps:
  //  - Wait for the previous frame to finish
  //  - Acquire an image from the swap chain
  //  - Record a command buffer which draws the scene onto that image
  //  - Submit the recorded command buffer
  //  - Present the swap chain image
  u64 current_frame = (this->frame_number++) % MAX_FRAMES_IN_FLIGHT;

  vkWaitForFences(device, 1, &inFlightFences[current_frame], VK_TRUE, UINT64_MAX);


  // Acquire the next image from the swapchain
  u32 imageIndex;
  auto result =
      vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphores[current_frame],
                            VK_NULL_HANDLE, &imageIndex);

  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebuffer_resized) {
    recreate_swapchain();
    return;
  } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    throw std::runtime_error("failed to acquire swap chain image!");
  }


  // imgui new frame
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplSDL2_NewFrame();

  ImGui::NewFrame();


  // imgui commands
  ImGui::ShowDemoWindow();

  vkResetCommandBuffer(commandBuffers[current_frame], 0);
  record_command_buffer(commandBuffers[current_frame], imageIndex);

  // Only reset the fence if we are submitting work
  vkResetFences(device, 1, &inFlightFences[current_frame]);



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


  // Now that we have an instance, allocate the vulkan allocator
  VmaVulkanFunctions vulkanFunctions = {};
  vulkanFunctions.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
  vulkanFunctions.vkGetDeviceProcAddr = &vkGetDeviceProcAddr;

  VmaAllocatorCreateInfo allocatorCreateInfo = {};
  allocatorCreateInfo.flags = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
  allocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_2;
  allocatorCreateInfo.physicalDevice = physicalDevice;
  allocatorCreateInfo.device = device;
  allocatorCreateInfo.instance = instance;
  allocatorCreateInfo.pVulkanFunctions = &vulkanFunctions;

  VkResult res = vmaCreateAllocator(&allocatorCreateInfo, &allocator);
}

ren::VulkanInstance::~VulkanInstance() {
  ImGui_ImplVulkan_Shutdown();

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
    vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
    vkDestroyFence(device, inFlightFences[i], nullptr);
  }

  // Clear the index/vertex/uniform buffer
  vertex_buffer.reset();
  index_buffer.reset();
  uniform_buffers.clear();

  vkDestroyDescriptorPool(device, descriptorPool, nullptr);
  vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);


  // Command Pool
  vkDestroyCommandPool(device, commandPool, nullptr);


  // texture
  vkDestroySampler(device, textureSampler, nullptr);
  vkDestroyImageView(device, textureImageView, nullptr);
  vkDestroyImage(device, textureImage, nullptr);
  vkFreeMemory(device, textureImageMemory, nullptr);


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

  vmaDestroyAllocator(allocator);


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
  int width, height;
  SDL_Vulkan_GetDrawableSize(window, &width, &height);
  this->extent.width = width;
  this->extent.height = height;


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



  auto bindingDescription = Vertex::get_binding_description();
  auto attributeDescriptions = Vertex::get_attribute_descriptions();

  // Vertex input
  VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
  vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputInfo.vertexBindingDescriptionCount = 1;
  vertexInputInfo.vertexAttributeDescriptionCount =
      static_cast<uint32_t>(attributeDescriptions.size());
  vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
  vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();


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
  rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

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
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
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
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  beginInfo.pInheritanceInfo = nullptr;

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
  VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
  renderPassInfo.clearValueCount = 1;
  renderPassInfo.pClearValues = &clearColor;

  vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

  // We can now bind the graphics pipeline:
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);


  update_uniform_buffer(imageIndex);


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



  ImGui::Begin("Vertex Editor");


  auto editVertex = [](ren::Vertex &v, const std::string &name) {
    ImGui::PushID(name.c_str());
    ImGui::Text("Vertex Editor: %s", name.c_str());
    ImGui::DragFloat3("Position", &v.pos.x, 0.01f);
    ImGui::ColorEdit3("Color", &v.color.r);
    ImGui::DragFloat2("TexCoord", &v.texCoord.x, 0.01f);
    ImGui::PopID();
  };

  editVertex(vertices[0], "Vertex 0");
  editVertex(vertices[1], "Vertex 1");
  editVertex(vertices[2], "Vertex 2");

  ImGui::End();

  // vertices[0].pos.x = sinf(SDL_GetTicks() / 1000.0f) * 0.3f;
  vertex_buffer->copyFromHost(vertices);
  index_buffer->copyFromHost(indices);

  VkBuffer vertexBuffers[] = {vertex_buffer->getHandle()};
  VkDeviceSize offsets[] = {0};


  vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
  vkCmdBindIndexBuffer(commandBuffer, index_buffer->getHandle(), 0, VK_INDEX_TYPE_UINT32);

  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1,
                          &descriptorSets[imageIndex], 0, nullptr);

  vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);



  // Now we can issue the draw command for the triangle
  vkCmdDraw(commandBuffer, 3, 1, 0, 0);


  // Right before we end the render pass, we need to render the ImGui draw data.
  ImGui::Render();
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

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

void ren::VulkanInstance::createVertexBuffer(void) {
  vertex_buffer = std::make_shared<ren::VertexBuffer<ren::Vertex>>(*this, vertices);
  index_buffer = std::make_shared<ren::IndexBuffer<u32>>(*this, indices);

  uniform_buffers.resize(MAX_FRAMES_IN_FLIGHT);

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    // Nice and simple with my API
    uniform_buffers[i] = std::make_shared<ren::UniformBuffer<ren::UniformBufferObject>>(*this, 1);
  }
}

void ren::VulkanInstance::transitionImageLayout(VkImage image, VkFormat format,
                                                VkImageLayout oldLayout, VkImageLayout newLayout) {
  VkCommandBuffer commandBuffer = beginSingleTimeCommands();

  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  barrier.srcAccessMask = 0;  // TODO
  barrier.dstAccessMask = 0;  // TODO

  VkPipelineStageFlags sourceStage;
  VkPipelineStageFlags destinationStage;

  if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else {
    throw std::invalid_argument("unsupported layout transition!");
  }

  vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1,
                       &barrier);

  endSingleTimeCommands(commandBuffer);
}




void ren::VulkanInstance::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width,
                                            uint32_t height) {
  VkCommandBuffer commandBuffer = beginSingleTimeCommands();

  VkBufferImageCopy region{};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;

  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;

  region.imageOffset = {0, 0, 0};
  region.imageExtent = {width, height, 1};

  vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                         &region);

  endSingleTimeCommands(commandBuffer);
}




void ren::VulkanInstance::create_descriptor_set_layout(void) {
  // Every binding needs to be described through a VkDescriptorSetLayoutBinding struct.
  VkDescriptorSetLayoutBinding uboLayoutBinding{};
  uboLayoutBinding.binding = 0;

  // The first two fields specify the binding used in the shader and
  // the type of descriptor, which is a uniform buffer object. It is
  // possible for the shader variable to represent an array of uniform
  // buffer objects, and descriptorCount specifies the number of
  // values in the array. This could be used to specify a
  // transformation for each of the bones in a skeleton for skeletal
  // animation, for example. Our MVP transformation is in a single
  // uniform buffer object, so we're using a descriptorCount of 1.
  uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  uboLayoutBinding.descriptorCount = 1;

  // We also need to specify in which shader stages the descriptor is
  // going to be referenced. The stageFlags field can be a combination
  // of VkShaderStageFlagBits values or the value
  // VK_SHADER_STAGE_ALL_GRAPHICS. In our case, we're only referencing
  // the descriptor from the vertex shader.
  uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  // The pImmutableSamplers field is only relevant for image sampling related descriptors, which
  // we'll look at later. You can leave this to its default value.
  uboLayoutBinding.pImmutableSamplers = nullptr;  // Optional


  VkDescriptorSetLayoutBinding samplerLayoutBinding{};
  samplerLayoutBinding.binding = 1;
  samplerLayoutBinding.descriptorCount = 1;
  samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  samplerLayoutBinding.pImmutableSamplers = nullptr;
  samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  std::array<VkDescriptorSetLayoutBinding, 2> bindings = {uboLayoutBinding, samplerLayoutBinding};

  VkDescriptorSetLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
  layoutInfo.pBindings = bindings.data();

  if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create descriptor set layout!");
  }
}



void ren::VulkanInstance::create_descriptor_pool(void) {
  std::array<VkDescriptorPoolSize, 2> poolSizes{};
  poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
  poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  poolSizes[1].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  poolInfo.pPoolSizes = poolSizes.data();
  poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

  if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
    throw std::runtime_error("failed to create descriptor pool!");
  }
}



void ren::VulkanInstance::create_descriptor_sets(void) {
  std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout);
  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = descriptorPool;
  allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
  allocInfo.pSetLayouts = layouts.data();

  descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
  if (vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
    throw std::runtime_error("failed to allocate descriptor sets!");
  }

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = uniform_buffers[i]->getHandle();
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(UniformBufferObject);

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = textureImageView;
    imageInfo.sampler = textureSampler;


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



    vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()),
                           descriptorWrites.data(), 0, nullptr);
  }
}


void ren::VulkanInstance::createTextureImage() {
  printf("Loading texture image...\n");
  int texWidth, texHeight, texChannels;
  stbi_uc *pixels =
      stbi_load("assets/actually.png", &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
  VkDeviceSize imageSize = texWidth * texHeight * 4;

  if (!pixels) { throw std::runtime_error("failed to load texture image!"); }

  {
    ren::Buffer stagingBuffer(
        *this, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    stagingBuffer.copyFromHost(pixels, imageSize);

    stbi_image_free(pixels);


    // now we have a staging buffer with the texture data, we can create the
    // actual texture image

    create_image(texWidth, texHeight, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, textureImage, textureImageMemory);

    transitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufferToImage(stagingBuffer.getHandle(), textureImage, static_cast<uint32_t>(texWidth),
                      static_cast<uint32_t>(texHeight));

    transitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_SRGB,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }

  // Now, create the texture image view
  textureImageView = create_image_view(textureImage, VK_FORMAT_R8G8B8A8_SRGB);

  // Texture Sampler
  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = samplerInfo.minFilter = VK_FILTER_NEAREST;

  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

  samplerInfo.anisotropyEnable = VK_FALSE;
  samplerInfo.maxAnisotropy = 1.0f;

  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.mipLodBias = 0.0f;
  samplerInfo.minLod = 0.0f;
  samplerInfo.maxLod = 0.0f;

  if (vkCreateSampler(device, &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS) {
    throw std::runtime_error("failed to create texture sampler!");
  }
}



VkImageView ren::VulkanInstance::create_image_view(VkImage image, VkFormat format) {
  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  VkImageView imageView;
  if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
    throw std::runtime_error("failed to create texture image view!");
  }

  return imageView;
}


void ren::VulkanInstance::create_image(uint32_t width, uint32_t height, VkFormat format,
                                       VkImageTiling tiling, VkImageUsageFlags usage,
                                       VkMemoryPropertyFlags properties, VkImage &image,
                                       VkDeviceMemory &imageMemory) {
  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = width;
  imageInfo.extent.height = height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = format;
  imageInfo.tiling = tiling;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = usage;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
    throw std::runtime_error("failed to create image!");
  }

  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(device, image, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = find_memory_type(memRequirements.memoryTypeBits, properties);

  if (vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
    throw std::runtime_error("failed to allocate image memory!");
  }

  vkBindImageMemory(device, image, imageMemory, 0);
}


VkCommandBuffer ren::VulkanInstance::beginSingleTimeCommands() {
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandPool = commandPool;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer commandBuffer;
  vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer(commandBuffer, &beginInfo);

  return commandBuffer;
}

void ren::VulkanInstance::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
  vkEndCommandBuffer(commandBuffer);

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;

  vkQueueSubmit(graphics_queue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(graphics_queue);

  vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}


struct Camera {
  glm::vec3 position = {0.0f, 0.0f, 3.0f};
  glm::vec3 angles = {0.0f, 0.0f, 0.0f};  // pitch, yaw, roll
  bool mouse_captured = false;
  bool first_update = true;

  glm::mat4 view_matrix() const {
    float pitch = angles.x;
    float yaw = angles.y;

    float sinPitch = sinf(pitch);
    float cosPitch = cosf(pitch);
    float sinYaw = sinf(yaw);
    float cosYaw = cosf(yaw);

    glm::vec3 forward(sinYaw * cosPitch,  // X component
                      sinPitch,           // Y component
                      -cosYaw * cosPitch  // Z component
    );

    glm::mat4 view = glm::lookAt(position, position + forward, glm::vec3(0.0f, 1.0f, 0.0f));
    return view;
  }

  void update(float dt) {
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    int mouse_x, mouse_y;
    SDL_GetRelativeMouseState(&mouse_x, &mouse_y);

    if (first_update) {
      // Capture mouse on first update
      SDL_SetRelativeMouseMode(mouse_captured ? SDL_TRUE : SDL_FALSE);
      first_update = false;
    }

    // Toggle mouse capture with ESC
    if (keys[SDL_SCANCODE_E]) {
      mouse_captured = !mouse_captured;
      SDL_SetRelativeMouseMode(mouse_captured ? SDL_TRUE : SDL_FALSE);
      SDL_Delay(100);  // Prevent rapid toggling
    }

    if (!mouse_captured) return;

    // Mouse look
    angles.y += mouse_x * 0.002f;  // yaw
    angles.x -= mouse_y * 0.002f;  // pitch

    // Clamp pitch
    if (angles.x > M_PI / 2.0f) angles.x = M_PI / 2.0f;
    if (angles.x < -M_PI / 2.0f) angles.x = -M_PI / 2.0f;

    // Movement vectors
    float cos_yaw = cosf(angles.y);
    float sin_yaw = sinf(angles.y);
    glm::vec3 forward = {sin_yaw, 0.0f, -cos_yaw};
    glm::vec3 right = {cos_yaw, 0.0f, sin_yaw};

    float speed = 1.0f * dt;

    // WASD movement
    if (keys[SDL_SCANCODE_W]) {
      // position.x += speed / 2.0f;
      position.x += forward.x * speed;
      position.z += forward.z * speed;
    }
    if (keys[SDL_SCANCODE_S]) {
      // position.x -= speed / 2.0f;
      position.x -= forward.x * speed;
      position.z -= forward.z * speed;
    }
    if (keys[SDL_SCANCODE_A]) {
      // position.z -= speed / 2.0f;
      position.x -= right.x * speed;
      position.z -= right.z * speed;
    }
    if (keys[SDL_SCANCODE_D]) {
      // position.z += speed / 2.0f;
      position.x += right.x * speed;
      position.z += right.z * speed;
    }
    if (keys[SDL_SCANCODE_SPACE]) { position.y += speed; }
    if (keys[SDL_SCANCODE_LSHIFT]) { position.y -= speed; }
  }
};

Camera camera;


void ren::VulkanInstance::update_uniform_buffer(u32 current_frame) {
  static auto startTime = std::chrono::high_resolution_clock::now();
  static auto lastTime = startTime;

  auto currentTime = std::chrono::high_resolution_clock::now();

  float time =
      std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

  auto deltaTime =
      std::chrono::duration<float, std::chrono::seconds::period>(currentTime - lastTime).count();
  lastTime = currentTime;



  camera.update(deltaTime);

  UniformBufferObject ubo{};
  ubo.model = glm::rotate(glm::mat4(1.0f), 0 * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
  ubo.view = camera.view_matrix();

  // glm::lookAt(glm::vec3(5.0f, 2.0f, sinf(time) + 2.0f), glm::vec3(0.0f, 0.0f, 0.0f),
  //                      glm::vec3(0.0f, 0.0f, 1.0f));
  ubo.proj =
      glm::perspective(glm::radians(90.0f), extent.width / (float)extent.height, 0.01f, 1000.0f);

  ubo.proj[1][1] *= -1;

  uniform_buffers[current_frame]->copyFromHost(&ubo, 1);
}


void ren::VulkanInstance::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                        VkMemoryPropertyFlags properties, VkBuffer &buffer,
                                        VkDeviceMemory &bufferMemory) {
  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
    throw std::runtime_error("failed to create buffer!");
  }

  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = find_memory_type(memRequirements.memoryTypeBits, properties);

  if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
    throw std::runtime_error("failed to allocate buffer memory!");
  }

  vkBindBufferMemory(device, buffer, bufferMemory, 0);
}


void ren::VulkanInstance::copy_buffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size,
                                      u32 srcOffset, u32 dstOffset) {
  VkCommandBuffer commandBuffer = beginSingleTimeCommands();

  VkBufferCopy copyRegion{};
  copyRegion.size = size;
  vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

  endSingleTimeCommands(commandBuffer);
}

u32 ren::VulkanInstance::find_memory_type(u32 typeFilter, VkMemoryPropertyFlags properties) {
  // First we need to query info about the available types of memory
  // using vkGetPhysicalDeviceMemoryProperties.
  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(physical_device, &memProperties);


  for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
    if ((typeFilter & (1 << i)) &&
        (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }

  throw std::runtime_error("failed to find suitable memory type!");
}

void ren::VulkanInstance::cleanup_swapchain(void) {
  for (size_t i = 0; i < swapchain_framebuffers.size(); i++) {
    vkDestroyFramebuffer(device, swapchain_framebuffers[i], nullptr);
  }

  for (size_t i = 0; i < image_views.size(); i++) {
    vkDestroyImageView(device, image_views[i], nullptr);
  }

  vkDestroySwapchainKHR(device, swapchain, nullptr);
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



void ren::VulkanInstance::init_imgui(void) {
  // 1: create descriptor pool for IMGUI
  // the size of the pool is very oversize, but it's copied from imgui demo itself.
  VkDescriptorPoolSize pool_sizes[] = {
      //
      {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
      {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
      {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
      {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000},
  };

  VkDescriptorPoolCreateInfo pool_info = {};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  pool_info.maxSets = 1000;
  pool_info.poolSizeCount = 11;  // size of pool_sizes
  pool_info.pPoolSizes = pool_sizes;

  VkDescriptorPool imguiPool;
  VK_CHECK(vkCreateDescriptorPool(device, &pool_info, nullptr, &imguiPool));


  // 2: initialize imgui library

  // this initializes the core structures of imgui
  ImGui::CreateContext();

  ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;



  // this initializes imgui for SDL
  ImGui_ImplSDL2_InitForVulkan(window);

  // this initializes imgui for Vulkan
  ImGui_ImplVulkan_InitInfo init_info = {};
  init_info.Instance = instance;
  init_info.PhysicalDevice = physical_device;
  init_info.Device = device;
  init_info.Queue = graphics_queue;
  init_info.DescriptorPool = imguiPool;
  init_info.MinImageCount = 3;
  init_info.ImageCount = 3;
  init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  init_info.RenderPass = render_pass;

  ImGui_ImplVulkan_Init(&init_info);



  ImGuiStyle *style = &ImGui::GetStyle();

  style->WindowPadding = ImVec2(5, 5);
  style->WindowRounding = 5.0f;
  style->FramePadding = ImVec2(3, 3);
  style->FrameRounding = 4.0f;
  style->ItemSpacing = ImVec2(4, 4);
  style->ItemInnerSpacing = ImVec2(3, 3);
  style->IndentSpacing = 25.0f;
  style->ScrollbarSize = 10.0f;
  style->ScrollbarRounding = 9.0f;
  style->GrabMinSize = 5.0f;
  style->GrabRounding = 3.0f;



  // execute a gpu command to upload imgui font textures
  // ImGui_ImplVulkan_CreateFontsTexture();

  // // clear font textures from cpu data
  // ImGui_ImplVulkan_DestroyFontUploadObjects();

  // // add the destroy the imgui created structures
  // _mainDeletionQueue.push_function([=]() {
  //   vkDestroyDescriptorPool(_device, imguiPool, nullptr);
  //   ImGui_ImplVulkan_Shutdown();
  // });
}
