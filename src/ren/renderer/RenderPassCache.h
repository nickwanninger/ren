#pragma once

#include <ren/types.h>
#include <vulkan/vulkan_core.h>
#include <ren/renderer/RenderTarget.h>

namespace ren {

  // In Ren, render passes should be used through a central "render pass cache"
  // system, which allows for efficient reuse of render passes across frames,
  // and if we need a new render pass for some reason, we can simply update our
  // Configuration structure, and request a new one. We will then garbage
  // collect those old passes which sit around for a while.


  // The thinking behind this structure is to allow for a mutable configuration
  // of a render pass, which can be used to create or fetch a render pass as needed.
  struct RenderPassConfiguration {
    ref<RenderTarget> target; // The render target this pass will use.

  };

  // This class roughly follows the singleton pattern. We allocate this on the Vulkan instance,
  // and you can access it through static methods mainly.
  class RenderPassCache {

  };

}