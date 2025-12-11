#include <ren/renderer/RenderPassCache.h>
#include <ren/renderer/Vulkan.h>



namespace ren {



  ref<RenderPass> RenderPassCache::get(RenderPass::Description &desc) {
    // Get the hash of the description.
    u64 hash = desc.hash();

    // Check if we already have this render pass in the cache.
    auto it = m_cache.find(hash);
    if (it != m_cache.end()) {
      // If we do, return the cached render pass.
      return it->second;
    } else {
      // Otherwise, create a new render pass and cache it.
      auto pass = make<RenderPass>(desc);
      m_cache[hash] = pass;
      return pass;
    }
  }

  RenderPassCache::~RenderPassCache(void) {
    m_cache.clear();
  }


}
