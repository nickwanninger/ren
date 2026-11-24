#pragma once
#include <ren/types.h>
#include <memory>

namespace ren {
  struct GraphVersion;
  class RenderGraph;


  // A reference to one version of a graph resource (see GraphVersion). It is
  // cheap to copy. Only the RenderGraph makes handles; a default handle is
  // null. A handle does not keep the producer of its version alive.
  class GraphHandle {
   public:
    GraphHandle() = default;

    explicit operator bool() const { return bool(version); }

    // True if both handles refer to the same version object. Two calls to
    // RenderGraph::previous() return different objects, so they are not equal.
    bool operator==(const GraphHandle &) const = default;


   private:
    explicit GraphHandle(std::shared_ptr<GraphVersion> value)
        : version(std::move(value)) {}

    std::shared_ptr<GraphVersion> version;
    friend class RenderGraph;
  };


  // A null handle, for an optional input.
  inline const GraphHandle nullGraphHandle;
}  // namespace ren
