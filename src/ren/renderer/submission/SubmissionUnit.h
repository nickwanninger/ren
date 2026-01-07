#pragma once

#include <ren/types.h>
#include <ren/renderer/submission/SubmissionQueue.h>
#include <ren/core/Arena.h>
#include <ren/renderer/Descriptors.h>

namespace ren {


  class CommandEncoder;

  // A submission unit wraps all the resources needed to submit work to the GPU
  // in an atomic block. For example, an in-flight frame, or an async compute
  // task.  It holds the command buffer, descriptor pools and other resources
  // needed for the submission.
  //
  // The main lifecycle of a submission unit is:
  // - begin(): prepare for recording commands
  // - record commands using the command encoder
  // - submitTo(queue): submit the recorded commands to the given queue
  //
  // After submission, the submission unit can be reused by calling begin()
  // again. Resources allocated from the submission unit (like descriptors) are
  // valid until the next begin() call. This means that a submission unit can
  // be used multiple times, but care must be taken to ensure that resources are
  // not used after they have been invalidated by a begin() call.
  class SubmissionUnit {
   public:
    SubmissionUnit();


    // Prepare the submission unit for a new 'frame' of work.
    ref<CommandEncoder> begin();
    // Finalize the submission unit and submit it to the given queue.  The
    // fields of SubmissionInfo are passed through, other than `cmds`, which is
    // totally overwritten.
    ref<Fence> submitTo(SubmissionQueue &queue, SubmissionInfo info = {});


    // TODO: multiple command buffers?
    auto getMainCommandEncoder(void) { return m_cmd; }


    ren::Arena &getArena(void) { return m_arena; }
    auto &getDescriptorAllocator(void) { return m_descriptorAllocator; }

   private:
    VkCommandBuffer m_vkCmd;
    ref<CommandEncoder> m_cmd;


    DescriptorAllocator m_descriptorAllocator;
    ren::Arena m_arena{4096, true};
  };
}  // namespace ren