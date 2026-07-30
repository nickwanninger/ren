#include <ren/renderer/submission/SubmissionUnit.h>
#include <ren/renderer/CommandEncoder.h>
#include <ren/renderer/submission/SubmissionQueue.h>
#include <ren/renderer/Renderer.h>
#include "ren/core/Instrumentation.h"

namespace ren {

#define NUM_QUERY_POOL_ENTRIES 1024

  SubmissionUnit::SubmissionUnit()
      : m_frameGlobalsBuffer(allocateBuffer<FrameGlobals>(
            1, BufferDomain::Upload, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)) {
    auto &vulkan = ren::getVulkan();
    // ---- Allocate the command buffer for this frame ---- //
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = vulkan.commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(vulkan.device, &allocInfo, &this->m_vkCmd);

    // Create the command encoder with reference to this submission unit
    m_cmd = ren::make<CommandEncoder>(m_vkCmd, *this);


    // Create query pool
    VkQueryPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = NUM_QUERY_POOL_ENTRIES,  // Start and end
    };
    vkCreateQueryPool(vulkan.device, &poolInfo, nullptr, &m_timestampQueryPool);

    // Grab the query timestamp period
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(getVulkan().physical_device, &props);
    this->timestampPeriod = props.limits.timestampPeriod;

    m_frameDescriptorSet =
        Renderer::get().getGlobalDescriptors().allocateFrameSet(
            m_frameGlobalsBuffer);
    updateFrameGlobals({});
  }

  SubmissionUnit::~SubmissionUnit() {
    auto& vulkan = getVulkan();
    m_cmd.reset();
    if (m_timestampQueryPool != VK_NULL_HANDLE) {
      vkDestroyQueryPool(vulkan.device, m_timestampQueryPool, nullptr);
      m_timestampQueryPool = VK_NULL_HANDLE;
    }
    if (m_vkCmd != VK_NULL_HANDLE) {
      vkFreeCommandBuffers(vulkan.device, vulkan.commandPool, 1, &m_vkCmd);
      m_vkCmd = VK_NULL_HANDLE;
    }
    Renderer::get().getGlobalDescriptors().freeFrameSet(m_frameDescriptorSet);
    m_frameDescriptorSet = VK_NULL_HANDLE;
  }


  ref<CommandEncoder> SubmissionUnit::begin() {
    REN_PROFILE_FUNCTION();
    m_cmd->reset();
    m_descriptorAllocator.reset();
    size_t allocatedLastTime = m_arena.clear();

    // begin the command buffer.
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;




    if (m_queries != nullptr && false) {
      // After submission:
      uint64_t timestamps[NUM_QUERY_POOL_ENTRIES];
      VK_CHECK(vkGetQueryPoolResults(getVulkan().device, m_timestampQueryPool, 0, nextQueryIndex, sizeof(timestamps), timestamps, sizeof(uint64_t),
                                     VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
      fmt::println("Timestamp Query results:\n");
      float total = 0.0f;
      for (auto *q = m_queries; q != nullptr; q = q->next) {
        auto ticks = timestamps[q->endIndex] - timestamps[q->startIndex];

        float milliseconds = (ticks * timestampPeriod) / 1e6f;
        fmt::println("  {:8.5f} ms   {:64s} {}-{}", milliseconds, q->name, q->startIndex, q->endIndex);
        total += milliseconds;
      }

      float fps = 1000.0f / total;
      fmt::println("Total time: {:8.5f} ms   FPS: {:8.2f}\n", total, fps);

      fmt::println("");
    }


    {
      REN_PROFILE_SCOPE("vkBeginCommandBuffer");
      if (vkBeginCommandBuffer(m_vkCmd, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer!");
      }
    }

    m_queries = nullptr;
    nextQueryIndex = 0;
    vkCmdResetQueryPool(m_vkCmd, m_timestampQueryPool, 0, NUM_QUERY_POOL_ENTRIES);

    return m_cmd;
  }

  void SubmissionUnit::updateFrameGlobals(const FrameGlobals &globals) {
    m_frameGlobalsBuffer.copyFromHost(&globals, sizeof(globals));
  }


  ref<Fence> SubmissionUnit::submitTo(SubmissionQueue &queue, SubmissionInfo info) {
    VK_CHECK(vkEndCommandBuffer(m_vkCmd));

    std::array<VkCommandBuffer, 1> cmds = {m_cmd->buf()};
    info.cmds = cmds;
    return queue.submit(info);
  }

}  // namespace ren
