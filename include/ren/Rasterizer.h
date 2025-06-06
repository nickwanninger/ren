#pragma once

#include <ren/RenderTarget.h>
#include <ren/geom.h>

namespace ren {



  class Rasterizer {
    Rasterizer(RenderTarget &render_target)
        : targ(render_target) {}

   private:
    RenderTarget &targ;
  };
}  // namespace ren
