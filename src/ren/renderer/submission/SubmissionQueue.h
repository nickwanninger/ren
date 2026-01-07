#pragma once

#include <ren/renderer/Fence.h>
#include <ren/types.h>
#include <span>

namespace ren {


  struct SubmissionInfo {
    std::span<VkCommandBuffer> cmds;
    std::span<VkSemaphore> waitSemaphores = {};
    std::span<VkPipelineStageFlags> waitStages = {};
    std::span<VkSemaphore> signalSemaphores = {};
  };

  class SubmissionQueue : public RefCounted<SubmissionQueue> {
   public:
    inline SubmissionQueue(VkQueue queue, u32 familyIndex)
        : queue(queue)
        , familyIndex(familyIndex) {}

    ~SubmissionQueue();

    void waitForIdle();

    inline u32 family() const { return familyIndex; }
    ref<Fence> submit(const SubmissionInfo &info);

    ref<Fence> submitOne(VkCommandBuffer cmd) { return submit({.cmds = {&cmd, 1}}); }

    inline VkQueue getHandle() const { return queue; }

   private:
    VkQueue queue;
    u32 familyIndex;
  };
}  // namespace ren