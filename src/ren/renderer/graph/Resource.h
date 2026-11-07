#pragma once
#include <ren/renderer/graph/Handle.h>
#include <ren/types.h>
#include <optional>

namespace ren {
  struct RecordContext;
  struct CapturedView;


  // How a task uses a resource. The graph makes barriers from these values
  // (see barrierInfo() in ImageResource.cpp).
  enum class GraphAccess : u8 {
    RenderTarget,            // Color attachment of a render pass. Reads and writes.
    DepthTarget,             // Depth attachment of a render pass. Reads and writes.
    VertexShaderRead,        // Sampled in a vertex shader.
    FragmentShaderRead,      // Sampled in a fragment shader.
    ComputeShaderRead,       // Read in a compute shader.
    ComputeShaderWrite,      // Written in a compute shader. The old contents are not read.
    ComputeShaderReadWrite,  // Read and written in a compute shader.
    ShaderRead               // Sampled at a stage that is not known. Waits on all commands.
  };


  // True if the access writes. A write waits for all earlier reads and writes.
  inline bool graphWrites(GraphAccess access) {
    return access == GraphAccess::RenderTarget || access == GraphAccess::DepthTarget || access == GraphAccess::ComputeShaderWrite ||
           access == GraphAccess::ComputeShaderReadWrite;
  }


  // The size of a graph image: a fixed size in pixels, or a scale of the
  // render size of the view.
  struct ImageSize {
    glm::uvec2 pixels{1};            // Used if scale is empty.
    std::optional<glm::vec2> scale;  // Multiplied by the render size of the view.

    static ImageSize absolute(glm::uvec2 pixels) { return {pixels, {}}; }
    static ImageSize viewRelative(float scale) { return {glm::uvec2(1), glm::vec2(scale)}; }

    // Return the size in pixels for this view. A view-relative size rounds
    // down, and each dimension is at least one. Throws for a view-relative
    // size without a view, or for a scale that is not positive.
    glm::uvec2 resolve(const CapturedView *view) const;
  };


  // The description of a graph image.
  struct GraphImageSpec {
    ImageSize size = ImageSize::viewRelative(1.0f);
    VkFormat format = VK_FORMAT_B8G8R8A8_SRGB;  // Ignored for depth attachments.
    bool temporal = false;                      // Keep the result of the last frame, for RenderGraph::previous().
  };


  // One allocation of a resource, e.g. one Vulkan image. Each storage keeps
  // its own access state, so barriers stay correct across frames.
  struct GraphStorage {
    virtual ~GraphStorage() = default;

    // Record a barrier from the earlier accesses to the next access.
    virtual void transition(RecordContext &, const std::vector<GraphAccess> &from, GraphAccess to) const = 0;

    // The last write, or the reads since the last write. Only
    // RenderGraph::submitted() changes it, so it shows only submitted work.
    std::vector<GraphAccess> access;
  };


  // The definition of a resource in the graph, e.g. an image with a size and
  // a format. Derive from it to add a new kind of resource. The graph decides
  // when to reuse storage, and calls allocate() when it needs new storage.
  struct GraphResource {
    virtual ~GraphResource() = default;

    // Make new storage for this view.
    virtual ref<GraphStorage> allocate(const CapturedView *view) const = 0;

    // True if the storage can be used for this view, e.g. it has the correct size.
    virtual bool compatible(const GraphStorage &, const CapturedView *view) const = 0;

    // TODO: Nothing calls this yet. Should the graph inspector show resources?
    virtual void inspect() const {}

    std::string name;
    bool temporal = false;  // See GraphImageSpec::temporal.

   private:
    // Only the graph changes these. See RenderGraph::assignStorage().
    std::weak_ptr<void> registeredOwner;  // The identity of the graph that owns this resource.
    ref<GraphStorage> current;            // The storage from the last frame where the producer ran.
    ref<GraphStorage> history;            // The same storage, for a temporal resource.
    std::vector<ref<GraphStorage>> pool;  // All storage that this resource owns. For a temporal resource: the history and one spare.
    friend class RenderGraph;
  };


  // One version of a resource: its contents after a specified task writes or
  // updates it. A GraphHandle refers to one version. All versions of a
  // resource use the same storage.
  struct GraphVersion {
    std::weak_ptr<void> owner;  // The identity of the graph.
    ref<GraphResource> resource;
    u64 id = 0;             // For error messages. A history handle has the ID of its version.
    u64 producer = 0;       // The ID of the task that writes this version. Zero until a task writes it.
    bool previous = false;  // True for a history handle from RenderGraph::previous().

    // The version that this version updates, or null for the first version.
    // The reference is strong, so a handle keeps the earlier versions alive.
    ref<GraphVersion> prior;

    // The update of this version, if there is one. The reference is weak, so
    // the versions do not form a reference cycle.
    std::weak_ptr<GraphVersion> successor;
  };


  // One input or output of a task: a handle and the access to it.
  struct GraphHandleUsage {
    GraphHandle handle;
    GraphAccess access;
  };
}  // namespace ren
