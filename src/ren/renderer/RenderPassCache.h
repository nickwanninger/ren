#pragma once

#include <ren/types.h>
#include <vulkan/vulkan_core.h>
#include <ren/renderer/RenderTarget.h>
#include <ren/renderer/RenderPass.h>

namespace ren {

  // In Ren, render passes should be used through a central "render pass cache"
  // system, which allows for efficient reuse of render passes across frames,
  // and if we need a new render pass for some reason, we can simply update our
  // Configuration structure, and request a new one. We will then garbage
  // collect those old passes which sit around for a while.



  class RenderPassCache {
   public:
    ~RenderPassCache(void);
    ref<RenderPass> get(RenderPass::Description &desc);

    void clearCache(void) { m_cache.clear(); }


   private:
    std::unordered_map<u64, ref<RenderPass>> m_cache;
  };

}  // namespace ren