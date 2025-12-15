#pragma once

#include <ren/renderer/Fence.h>
#include <ren/types.h>
#include <span>

namespace ren {

  class SubmissionQueue : public RefCounted<SubmissionQueue> {
   public:
    inline SubmissionQueue(VkQueue queue, u32 familyIndex)
        : queue(queue)
        , familyIndex(familyIndex) {}

    ~SubmissionQueue();

    void waitForIdle();

    inline u32 family() const { return familyIndex; }

    // Submit a raw VkCommandBuffer, assuming it is already ended.
    ref<Fence> submit(VkCommandBuffer cmd);
    ref<Fence> submit(std::span<VkCommandBuffer> cmds);

    inline VkQueue getHandle() const { return queue; }

   private:
    VkQueue queue;
    u32 familyIndex;
  };
}  // namespace ren