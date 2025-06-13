#include <ren/Vulkan.h>
#include <ren/Shader.h>

#include <vector>
#include <fmt/core.h>
#include <fstream>
#include "imgui.h"
#include "vkb/VkBootstrap.h"
#include "vulkan/vulkan_core.h"
#include <SDL2/SDL_vulkan.h>

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

  createUniformBuffers();
  init_renderpass();


  init_command_pool();

  createDepthResources();
  createTextureImage(); // TODO
  init_framebuffers();

  init_command_buffer();
  init_sync_objects();


  init_imgui();
}




VkCommandBuffer ren::VulkanInstance::beginFrame(void) {
  this->imageIndex = (this->frame_number++) % MAX_FRAMES_IN_FLIGHT;

  vkWaitForFences(device, 1, &inFlightFences[imageIndex], VK_TRUE, UINT64_MAX);


  // Acquire the next image from the swapchain

  auto result =
      vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphores[imageIndex],
                            VK_NULL_HANDLE, &imageIndex);

  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebuffer_resized) {
    recreate_swapchain();
    return VK_NULL_HANDLE;
  } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    throw std::runtime_error("failed to acquire swap chain image!");
  }

  auto commandBuffer = commandBuffers[imageIndex];


  // imgui new frame
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplSDL2_NewFrame();

  ImGui::NewFrame();


  // imgui commands
  ImGui::ShowDemoWindow();

  vkResetCommandBuffer(commandBuffer, 0);


  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  beginInfo.pInheritanceInfo = nullptr;

  if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
    throw std::runtime_error("failed to begin recording command buffer!");
  }



  // Finally, start the render pass
  VkRenderPassBeginInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassInfo.renderPass = render_pass;
  renderPassInfo.framebuffer = swapchain_framebuffers[imageIndex];
  renderPassInfo.renderArea.offset = {0, 0};
  renderPassInfo.renderArea.extent = extent;

  // setup the clear values

  std::array<VkClearValue, 2> clearValues{};
  clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  clearValues[1].depthStencil = {1.0f, 0};

  renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
  renderPassInfo.pClearValues = clearValues.data();

  vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);


  // oh.. do this stuff too.
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



  return commandBuffer;
}

void ren::VulkanInstance::endFrame(void) {
  auto commandBuffer = commandBuffers[imageIndex];



  // Right before we end the render pass, we need to render the ImGui draw data.
  ImGui::Render();
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

  // The render pass can now be ended:
  vkCmdEndRenderPass(commandBuffer);

  // And we've finished recording the command buffer:
  if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
    throw std::runtime_error("failed to record command buffer!");
  }



  // Only reset the fence if we are submitting work
  vkResetFences(device, 1, &inFlightFences[imageIndex]);



  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

  VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[imageIndex]};
  VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = waitSemaphores;
  submitInfo.pWaitDstStageMask = waitStages;

  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffers[imageIndex];

  VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[imageIndex]};
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSemaphores;

  if (vkQueueSubmit(graphics_queue, 1, &submitInfo, inFlightFences[imageIndex]) != VK_SUCCESS) {
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
  uint32_t index = imageIndex;  // we need a u32
  presentInfo.pImageIndices = &index;

  presentInfo.pResults = nullptr;  // Optional

  vkQueuePresentKHR(graphics_queue, &presentInfo);

  imageIndex = (imageIndex + 1) % MAX_FRAMES_IN_FLIGHT;
}

void ren::VulkanInstance::draw_frame(void) {
  // Not Needed
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
  uniform_buffers.clear();


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



  VkAttachmentDescription depthAttachment{};
  depthAttachment.format = findDepthFormat();
  depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference depthAttachmentRef{};
  depthAttachmentRef.attachment = 1;
  depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorAttachmentRef;
  subpass.pDepthStencilAttachment = &depthAttachmentRef;


  VkSubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcAccessMask = 0;
  dependency.srcStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.dstStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.dstAccessMask =
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;



  std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};
  VkRenderPassCreateInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
  renderPassInfo.pAttachments = attachments.data();
  renderPassInfo.subpassCount = 1;
  renderPassInfo.pSubpasses = &subpass;
  renderPassInfo.dependencyCount = 1;
  renderPassInfo.pDependencies = &dependency;

  if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &render_pass) != VK_SUCCESS) {
    throw std::runtime_error("failed to create render pass!");
  }
}



void ren::VulkanInstance::init_framebuffers(void) {
  swapchain_framebuffers.resize(image_views.size());

  for (size_t i = 0; i < image_views.size(); i++) {
    if (image_views[i] == VK_NULL_HANDLE) {
      throw std::runtime_error("Image view is null for swapchain image " + std::to_string(i));
    }
    if (depthImageView == VK_NULL_HANDLE) { throw std::runtime_error("Depth image view is null"); }
    std::array<VkImageView, 2> attachments = {image_views[i], depthImageView};

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = render_pass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
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

void ren::VulkanInstance::createUniformBuffers(void) {
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




VkFormat ren::VulkanInstance::findSupportedFormat(const std::vector<VkFormat> &candidates,
                                                  VkImageTiling tiling,
                                                  VkFormatFeatureFlags features) {
  for (VkFormat format : candidates) {
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(this->physical_device, format, &props);

    if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
      return format;
    } else if (tiling == VK_IMAGE_TILING_OPTIMAL &&
               (props.optimalTilingFeatures & features) == features) {
      return format;
    }
  }

  throw std::runtime_error("failed to find supported format!");
}


bool hasStencilComponent(VkFormat format) {
  return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}


void ren::VulkanInstance::createDepthResources() {
  auto depthFormat = findDepthFormat();

  create_image(extent.width, extent.height, depthFormat, VK_IMAGE_TILING_OPTIMAL,
               VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
               depthImage, depthImageMemory);
  depthImageView = create_image_view(depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
}


void ren::VulkanInstance::createTextureImage() {
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



VkImageView ren::VulkanInstance::create_image_view(VkImage image, VkFormat format,
                                                   VkImageAspectFlags aspectFlags) {
  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = aspectFlags;
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



void ren::VulkanInstance::update_uniform_buffer(u32 current_frame) {
  // Not Needed
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
  vkDestroyImageView(device, depthImageView, nullptr);
  vkDestroyImage(device, depthImage, nullptr);
  vkFreeMemory(device, depthImageMemory, nullptr);

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
