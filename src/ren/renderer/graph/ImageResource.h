#pragma once
#include <ren/renderer/Image.h>
#include <ren/renderer/graph/Resource.h>
#include <ren/renderer/Sampler.h>
#include <ren/renderer/TextureHandle.h>

namespace ren {


  // The storage of an image resource: one Vulkan image and its sampler.
  struct ImageStorage : GraphStorage {
    ImageRef image;
    ref<Sampler> sampler;  // The sampler for textureHandle().
    bool depth = false;    // A depth image. Barriers use the depth aspect.

    // Releases the bindless slot of the image. Submissions retain storage, so
    // the last release happens after GPU completion.
    ~ImageStorage() override;

    // Return the bindless handle of the image and its sampler. The first call
    // adds the image to the bindless heap.
    TextureHandle textureHandle() const;

    // Record one image barrier from the last accesses to the next access. The
    // accesses in from are one write or several reads, so they have one layout.
    // If from is empty, the image is new, and its old contents are discarded.
    void transition(RecordContext &, const std::vector<GraphAccess> &from, GraphAccess to) const override;
  };


  // An image in the graph. Each frame, the graph uses it to make sure that
  // existing storage still fits, or to allocate new storage.
  struct ImageResource : GraphResource {
    // The initial access is the access of the task that creates the image. It
    // sets the type (color or depth) and the usage flags of the image.
    ImageResource(const GraphImageSpec &spec, GraphAccess initial);

    // Make a new image with the size of the spec for this view.
    ref<GraphStorage> allocate(const CapturedView *view) const override;

    // True if the storage has the size and format of the spec for this view.
    bool compatible(const GraphStorage &, const CapturedView *view) const override;

    GraphImageSpec spec;
    GraphAccess initialAccess;
    ref<Sampler> sampler;  // The default sampler, shared by all storage of this resource.
  };
}  // namespace ren
