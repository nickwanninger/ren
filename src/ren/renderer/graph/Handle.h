#pragma once

#include <ren/types.h>

namespace ren {
  // A graph handle is an identifier for a resource in the render graph.
  using GraphHandle = u32;


  // A null graph handle constant. This is reserved to indicate an invalid or null handle.
  constexpr GraphHandle nullGraphHandle = 0;

  // -- Reserved handles --
  constexpr GraphHandle reservedGraphHandleStart = nullGraphHandle + 1;
  constexpr GraphHandle swapchainGraphHandle = reservedGraphHandleStart + 1;  // 2
  // Add more reserved handles here as needed.

  constexpr GraphHandle userGraphHandleStart = 255;
}  // namespace ren