#pragma once

#include <ren/types.h>
#include <ren/renderer/Image.h>
#include <ren/renderer/Texture.h>
#include <ren/renderer/RenderTarget.h>
#include <ren/renderer/Descriptors.h>

namespace ren {


  // TODO: Move me elsewhere!



  struct TimestampMeasurement {
    std::string name;
    double duration = 0.0;  // milliseconds
  };

  class FramePerformanceTracker {
   public:
    static constexpr u32 maxTimestampQueries = 64;

    // This class manages performance queries for the renderer's per-frame data.
    // It manages a query pool

    inline FramePerformanceTracker() {
      currentQuery = 0;
      auto &vulkan = ren::getVulkan();
      VkQueryPoolCreateInfo queryPoolInfo = {};
      queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
      queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
      queryPoolInfo.queryCount = maxTimestampQueries;
      VK_CHECK(vkCreateQueryPool(vulkan.device, &queryPoolInfo, nullptr, &timestampQueryPool));

      // Allocate results buffer
      timestampResults.resize(maxTimestampQueries);
      pendingQueries.reserve(maxTimestampQueries / 2);  // Assume pairs of begin/end
    }

    inline ~FramePerformanceTracker() {
      auto &vulkan = ren::getVulkan();
      vkDestroyQueryPool(vulkan.device, timestampQueryPool, nullptr);
    }


    void begin(VkCommandBuffer cmdBuffer, const std::string &name,
               VkPipelineStageFlagBits stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT) {
      assert(currentQuery < maxTimestampQueries);

      vkCmdWriteTimestamp(cmdBuffer, stage, timestampQueryPool, currentQuery);
      activeQueries[name] = currentQuery;
      currentQuery++;
    }

    void end(VkCommandBuffer cmdBuffer, const std::string &name,
             VkPipelineStageFlagBits stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT) {
      assert(currentQuery < maxTimestampQueries);

      auto it = activeQueries.find(name);
      assert(it != activeQueries.end());  // Must have called beginTimestamp first

      vkCmdWriteTimestamp(cmdBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestampQueryPool,
                          currentQuery);

      PendingQuery query;
      query.name = name;
      query.beginIndex = it->second;
      query.endIndex = currentQuery;
      pendingQueries.push_back(query);

      activeQueries.erase(it);
      currentQuery++;
    }

    std::vector<TimestampMeasurement> nextFrame(VkCommandBuffer cmd) {
      auto &vulkan = ren::getVulkan();

      std::vector<TimestampMeasurement> measurements = getResults();


      vkCmdResetQueryPool(cmd, timestampQueryPool, 0, maxTimestampQueries);
      currentQuery = 0;
      activeQueries.clear();
      pendingQueries.clear();

      return measurements;
    }

    std::vector<TimestampMeasurement> getResults() {
      auto &vulkan = ren::getVulkan();
      // TODO: CACHE THIS
      VkPhysicalDeviceProperties deviceProps;
      vkGetPhysicalDeviceProperties(vulkan.physical_device, &deviceProps);

      std::vector<TimestampMeasurement> results;

      if (pendingQueries.empty()) { return results; }

      // Get all timestamp results in one call
      uint32_t queryCount = currentQuery;
      VkResult result = vkGetQueryPoolResults(vulkan.device, timestampQueryPool, 0, queryCount,
                                              queryCount * sizeof(uint64_t),
                                              timestampResults.data(), sizeof(uint64_t),
                                              VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);

      if (result != VK_SUCCESS) {
        return results;  // Return empty if queries not ready
      }

      results.reserve(pendingQueries.size());

      for (const auto &query : pendingQueries) {
        uint64_t beginTime = timestampResults[query.beginIndex];
        uint64_t endTime = timestampResults[query.endIndex];
        uint64_t ticksDiff = endTime - beginTime;
        double nanoseconds = (double)ticksDiff * deviceProps.limits.timestampPeriod;
        double milliseconds = nanoseconds / 1000000.0;

        results.push_back({query.name, milliseconds});
      }

      return results;
    }


   private:
    struct PendingQuery {
      std::string name;
      uint32_t beginIndex;
      uint32_t endIndex;
    };
    VkQueryPool timestampQueryPool = VK_NULL_HANDLE;
    uint32_t currentQuery;
    float timestampPeriod;
    std::vector<uint64_t> timestampResults;
    std::unordered_map<std::string, uint32_t> activeQueries;
    std::vector<PendingQuery> pendingQueries;
  };

  struct UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
  };

  class Swapchain;

  // This is the data that holds all the per-frame data for the swapchain.
  struct FrameData {
    // Which of the frames in flight this is?
    u32 frameIndex;

    // The swapchain render target
    RenderTargetRef renderTarget = nullptr;

    // We then have a device image, which is the final image that is presented
    // to the device in the end. We will blit the render image to this
    // with some fancy up scaling and whatnot.
    ren::ImageRef deviceImage = nullptr;
    ren::ImageRef depthImage = nullptr;  // The depth buffer for rendering.

    // Semaphores for synchronizing the rendering process.

    // Signals when the image is ready to be rendered to.
    VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
    // Signals when the rendering is finished and the image is ready to be presented.
    VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
    // Fence to ensure that the GPU has finished rendering before we can
    // submit the next frame.
    VkFence inFlightFence = VK_NULL_HANDLE;

    FramePerformanceTracker perf;

    // ---- Per frame data, reset at the start of each frame ---- //
    // The command buffer that we record the rendering commands into.
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    DescriptorAllocator descriptorAllocator;

    // Query pool for GPU performance queries.
    constexpr static u32 query_count = 2;
    VkQueryPool queryPool = VK_NULL_HANDLE;

    FrameData(u32 frameIndex, Swapchain &sc, VkImage swapchainImage,
              VkImageView swapchainImageView);
    ~FrameData();


    // timestamp query
    std::vector<u64> getQueryResults(void);
  };
}  // namespace ren