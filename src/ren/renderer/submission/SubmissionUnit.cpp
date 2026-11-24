#include <ren/renderer/submission/SubmissionUnit.h>
#include <ren/renderer/CommandEncoder.h>
#include <ren/renderer/submission/SubmissionQueue.h>
#include <ren/renderer/Renderer.h>
#include "ren/core/Instrumentation.h"

namespace ren {

#define NUM_QUERY_POOL_ENTRIES 1024

  SubmissionUnit::SubmissionUnit()
      : m_frameGlobals(
            Renderer::get().getFrameGlobalsAllocator().allocate()) {
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
  }


  ref<CommandEncoder> SubmissionUnit::begin() {
    REN_PROFILE_FUNCTION();
    VK_CHECK(vkResetCommandBuffer(m_vkCmd, 0));
    m_cmd->reset();
    retained.clear();
    m_descriptorAllocator.reset();
    // The queries live in the arena, so read them back before clearing it.
    writeQueriesToTrace();
    size_t allocatedLastTime = m_arena.clear();

    // begin the command buffer.
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;




    {
      REN_PROFILE_SCOPE("vkBeginCommandBuffer");
      if (vkBeginCommandBuffer(m_vkCmd, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer!");
      }
    }

    m_queries = nullptr;
    nextQueryIndex = 0;
    vkCmdResetQueryPool(m_vkCmd, m_timestampQueryPool, 0, NUM_QUERY_POOL_ENTRIES);

    // A query that spans the full command buffer. submitTo() writes its end.
    m_submissionQuery = newQuery("Submission");
    vkCmdWriteTimestamp(m_vkCmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_timestampQueryPool, m_submissionQuery->startIndex);
    m_submitFlowId = 0;

    return m_cmd;
  }

  // The tid of the GPU track in the trace. The CPU tids of FTR count up from 0,
  // so this value does not collide with them.
  static constexpr u64 kGpuTraceTid = 0x47505500;  // "GPU"

  static ftr_str_t submissionFlowName() {
    static ftr_str_t name = 0;
    if (name == 0) {
      name = ftr_intern_string("Submission");
    }
    return name;
  }

  static ftr_str_t completionFlowName() {
    static ftr_str_t name = 0;
    if (name == 0) {
      name = ftr_intern_string("Completion");
    }
    return name;
  }

  void SubmissionUnit::writeQueriesToTrace() {
    auto &vulkan = getVulkan();
    if (m_queries == nullptr || vulkan.getCalibratedTimestamps == nullptr) {
      return;
    }
    REN_PROFILE_FUNCTION();

    // Each entry is a (timestamp, availability) pair.
    struct QueryResult {
      u64 value;
      u64 available;
    };
    QueryResult results[NUM_QUERY_POOL_ENTRIES];
    // No WAIT_BIT. The caller must wait for the previous submission before
    // begin(). A query without an end timestamp is unavailable, and it does
    // not block.
    VkResult res = vkGetQueryPoolResults(vulkan.device, m_timestampQueryPool, 0, nextQueryIndex,
                                         sizeof(results), results, sizeof(QueryResult),
                                         VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if (res != VK_SUCCESS && res != VK_NOT_READY) {
      return;
    }

    // Get a GPU tick and a CPU time for the same instant. ftr_now_ns() reads
    // CLOCK_MONOTONIC_RAW.
    VkCalibratedTimestampInfoKHR infos[2] = {
        {.sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR, .timeDomain = VK_TIME_DOMAIN_DEVICE_KHR},
        {.sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR, .timeDomain = VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_KHR},
    };
    u64 calibration[2];
    u64 maxDeviation;
    if (vulkan.getCalibratedTimestamps(vulkan.device, 2, infos, calibration, &maxDeviation) != VK_SUCCESS) {
      return;
    }
    const u64 gpuBase = calibration[0];
    const u64 cpuBase = calibration[1];
    auto toCpuNs = [&](u64 gpuTicks) -> u64 {
      double deltaNs = (double)(i64)(gpuTicks - gpuBase) * timestampPeriod;
      return (u64)((i64)cpuBase + (i64)deltaNs);
    };

    static bool namedGpuThread = false;
    if (!namedGpuThread) {
      ftr_set_thread_name(kGpuTraceTid, "GPU");
      namedGpuThread = true;
    }

    const u64 pid = ftr_get_pid();
    for (auto *q = m_queries; q != nullptr; q = q->next) {
      const auto &start = results[q->startIndex];
      const auto &end = results[q->endIndex];
      if (!start.available || !end.available) {
        continue;
      }
      // Separate begin and end records, not ftr_write_span. With a complete
      // span, Perfetto binds the "Completion" flow below to the last pass and
      // not to the "Submission" span. See ftr_write_begin.
      ftr_write_begin(pid, kGpuTraceTid, q->name, toCpuNs(start.value));
      ftr_write_end(pid, kGpuTraceTid, q->name, toCpuNs(end.value));

      if (q == m_submissionQuery && m_submitFlowId != 0) {
        // 1ns inside each end, so the flows bind to the "Submission" span.
        ftr_write_flow_end(pid, kGpuTraceTid, submissionFlowName(),
                           toCpuNs(start.value) + 1, m_submitFlowId);

        // Connect the end of the GPU work to this begin(), which reuses the
        // unit.
        u64 completionFlowId = ftr_new_flow_id();
        ftr_write_flow_begin(pid, kGpuTraceTid, completionFlowName(),
                             toCpuNs(end.value) - 1, completionFlowId);
        ftr_write_flow_endi(completionFlowName(), completionFlowId);
      }
    }
  }

  ref<Fence> SubmissionUnit::submitTo(SubmissionQueue &queue, SubmissionInfo info) {
    REN_PROFILE_SCOPE("Submit");
    vkCmdWriteTimestamp(m_vkCmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_timestampQueryPool, m_submissionQuery->endIndex);
    VK_CHECK(vkEndCommandBuffer(m_vkCmd));

    // Connect this CPU span to the "Submission" span on the GPU track.
    // writeQueriesToTrace() writes the end of the flow.
    m_submitFlowId = ftr_new_flow_id();
    ftr_write_flow_begini(submissionFlowName(), m_submitFlowId);

    std::array<VkCommandBuffer, 1> cmds = {m_cmd->buf()};
    info.cmds = cmds;
    return queue.submit(info);
  }

}  // namespace ren
