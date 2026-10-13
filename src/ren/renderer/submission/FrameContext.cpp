#include <ren/renderer/submission/FrameContext.h>
#include <ren/renderer/submission/SubmissionQueue.h>

namespace ren {

  FrameContext::FrameContext(u32 slotIndex)
      : SubmissionUnit()
      , slotIndex(slotIndex) {}

  FrameContext::~FrameContext() {
    // Wait for any pending work before cleanup
    waitForFence();
  }

  void FrameContext::waitForFence() {
    REN_PROFILE_FUNCTION();
    if (m_inFlightFence) {
      m_inFlightFence->awaitCompletion(true);
      // The fence has been waited on and reset, so it no longer represents
      // pending work. Drop it so a later wait doesn't block forever on a fence
      // nothing will signal.
      m_inFlightFence.reset();
    }
  }

  ref<CommandEncoder> FrameContext::beginFrame() {
    // Just call base class begin() to reset resources and start recording
    return begin();
  }

  void FrameContext::submitFrame(SubmissionQueue &queue, SubmissionInfo info) {
    REN_PROFILE_SCOPE("Submit Graphics Queue");
    m_inFlightFence = submitTo(queue, info);
  }

}  // namespace ren
