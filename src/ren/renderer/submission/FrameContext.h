#pragma once

#include <ren/types.h>
#include <ren/renderer/submission/SubmissionUnit.h>

namespace ren {

  class SubmissionQueue;

  // The resources of one frame in flight: the command buffer, arena,
  // descriptors and frame globals of SubmissionUnit, plus the fence of the
  // frame's last submission.
  //
  // The Renderer owns MAX_FRAMES_IN_FLIGHT of these and uses them in turn. A
  // FrameContext does not know about swapchains, so a frame does not need a
  // window.
  class FrameContext : public SubmissionUnit {
   public:
    explicit FrameContext(u32 slotIndex);
    ~FrameContext();

    // Wait for this frame slot's previous work to complete
    void waitForFence();

    // Begin a new frame: reset resources and start command recording
    ref<CommandEncoder> beginFrame();

    // Submit the recorded commands and keep the fence for waitForFence().
    void submitFrame(SubmissionQueue &queue, SubmissionInfo info);

    void extendCompletion(SubmissionQueue &queue);

    // In [0, MAX_FRAMES_IN_FLIGHT).
    const u32 slotIndex;

   private:
    ref<Fence> m_inFlightFence = nullptr;  // Fence from last submission
  };


  // Global accessor for the current frame context
  FrameContext &getFrameUnit(void);

  inline u32 getFrameIndex(void) { return getFrameUnit().slotIndex; }

  template <typename T, typename... Args>
  inline T *frameAlloc(Args &&...args) {
    return getFrameUnit().getArena().push<T>(std::forward<Args>(args)...);
  }

}  // namespace ren
