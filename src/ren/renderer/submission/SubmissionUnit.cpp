#include <ren/renderer/submission/SubmissionUnit.h>
#include <ren/renderer/CommandEncoder.h>
#include <ren/renderer/submission/SubmissionQueue.h>
#include "ren/core/Instrumentation.h"

namespace ren {



  SubmissionUnit::SubmissionUnit() {
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

    {
      REN_PROFILE_SCOPE("vkBeginCommandBuffer");
      if (vkBeginCommandBuffer(m_vkCmd, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer!");
      }
    }

    return m_cmd;
  }



  ShaderObject &SubmissionUnit::createShaderObject(ref<ShaderProgram> &program) {
    // Allocate a new ShaderObject from this submission unit's arena.
    auto *mem = m_arena.push<ShaderObject>(program, *this);
    return *mem;
  }


  ref<Fence> SubmissionUnit::submitTo(SubmissionQueue &queue, SubmissionInfo info) {
    REN_PROFILE_FUNCTION();
    VK_CHECK(vkEndCommandBuffer(m_vkCmd));

    std::array<VkCommandBuffer, 1> cmds = {m_cmd->buf()};
    info.cmds = cmds;
    return queue.submit(info);
  }

}  // namespace ren