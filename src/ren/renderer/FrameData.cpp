#include <ren/renderer/FrameData.h>
#include <ren/renderer/Vulkan.h>
#include <ren/renderer/Swapchain.h>
#include <ren/core/Application.h>
#include <fmt/core.h>


namespace ren {
  FrameData::FrameData(u32 frameIndex, Swapchain &sc, VkImage swapchainImage,
                       VkImageView swapchainImageView) {
    REN_PROFILE_FUNCTION();

    auto &app = ren::Application::get();
    this->frameIndex = frameIndex;
    auto &vulkan = ren::getVulkan();
    // ---- Add the swapchain image to the deviceImage in the framedata ---- //
    VkImageCreateInfo imageCreateInfo = {};  // Just so the ren::Image class can have it.
    imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = sc.imageFormat;
    imageCreateInfo.extent.width = sc.deviceExtent.width;
    imageCreateInfo.extent.height = sc.deviceExtent.height;
    imageCreateInfo.extent.depth = 1;

    this->deviceImage = ren::Image::create(fmt::format("device #{}", frameIndex), swapchainImage,
                                           swapchainImageView,
                                           VK_NULL_HANDLE,  // Null allocation is a little strange.
                                           imageCreateInfo);

    this->depthImage = ren::ImageBuilder(fmt::format("depth #{}", frameIndex))
                           .setWidth(sc.deviceExtent.width)
                           .setHeight(sc.deviceExtent.height)
                           .setFormat(sc.depthFormat)
                           .setUsage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
                           .setViewAspectMask(VK_IMAGE_ASPECT_DEPTH_BIT)
                           .build();


    RenderTargetDescription renderTargetDesc;
    renderTargetDesc.setupColorAndDepth(this->deviceImage, sc.imageFormat, this->depthImage,
                                        sc.depthFormat);


    this->renderTarget =
        makeRef<RenderTarget>(renderTargetDesc, Renderer::get().getRenderPassRef());



    // ---- Allocate the semaphores and fence for this frame ---- //
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VK_CHECK(
        vkCreateSemaphore(vulkan.device, &semaphoreInfo, nullptr, &this->imageAvailableSemaphore));
    VK_CHECK(
        vkCreateSemaphore(vulkan.device, &semaphoreInfo, nullptr, &this->renderFinishedSemaphore));

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // Start signaled
    VK_CHECK(vkCreateFence(vulkan.device, &fenceInfo, nullptr, &this->inFlightFence));


    // ---- Allocate the command buffer for this frame ---- //
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = vulkan.commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(vulkan.device, &allocInfo, &this->commandBuffer);

    // Allocate the descriptor pool for this frame
    // the size of the pool is very oversized.
    // TODO: Make this dynamic somehow.
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

    VK_CHECK(vkCreateDescriptorPool(vulkan.device, &pool_info, nullptr, &descriptorPool));


    // Create timestamp query pool
    VkQueryPoolCreateInfo queryPoolInfo = {};
    queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryPoolInfo.queryCount = this->query_count;

    VK_CHECK(vkCreateQueryPool(vulkan.device, &queryPoolInfo, nullptr, &queryPool));

    auto cmd = vulkan.beginSingleTimeCommands();
    vkCmdResetQueryPool(cmd, queryPool, 0, query_count);
    vulkan.endSingleTimeCommands(cmd);
  }


  FrameData::~FrameData() {
    auto &vulkan = ren::getVulkan();

    vkDestroyQueryPool(vulkan.device, this->queryPool, nullptr);

    // This needs to be done because the ImageView is not managed by the Swapchain
    vkDestroyImageView(vulkan.device, this->deviceImage->getImageView(), nullptr);
    this->depthImage.reset();
    this->deviceImage.reset();
    this->renderTarget.reset();
    vkDestroyDescriptorPool(vulkan.device, this->descriptorPool, nullptr);
    vkDestroySemaphore(vulkan.device, this->imageAvailableSemaphore, nullptr);
    vkDestroySemaphore(vulkan.device, this->renderFinishedSemaphore, nullptr);
    vkDestroyFence(vulkan.device, this->inFlightFence, nullptr);
  }

  std::vector<u64> FrameData::getQueryResults(void) {
    std::vector<u64> results = {};
    results.resize(query_count, 0);
    if (query_count > 0) {
      auto &vulkan = ren::getVulkan();
      vkGetQueryPoolResults(vulkan.device, this->queryPool, 0, query_count,
                            sizeof(u64) * query_count, results.data(), sizeof(u64),
                            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    }
    return results;
  }
}  // namespace ren