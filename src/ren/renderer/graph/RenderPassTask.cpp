#include <ren/renderer/graph/RenderPassTask.h>
#include <ren/renderer/graph/RenderGraph.h>


namespace ren {


  // Implementation of RenderPassTask, which represents a render pass in the render graph.


  GraphHandle RenderPassTask::addColorAttachment(const std::string_view &name,
                                                 const GraphImageSpec &spec) {
    RenderGraph &graph = this->graph();
    GraphHandle handle = graph.createImage(name, spec, GraphAccess::RenderTarget);
    this->write(handle, GraphAccess::RenderTarget);

    // Store the attachment handle
    this->attachmentHandles[std::string(name)] = handle;

    // Update the render pass description
    this->desc.addColorAttachment(name, spec.format);
    return handle;
  }


  GraphHandle RenderPassTask::addDepthAttachment(const std::string_view &name,
                                                 const GraphImageSpec &uspec) {
    GraphImageSpec spec = uspec; // Copy to modify if needed.


    RenderGraph &graph = this->graph();
    spec.format = getVulkan().findDepthFormat();
    GraphHandle handle = graph.createImage(name, spec, GraphAccess::DepthTarget);
    this->write(handle, GraphAccess::DepthTarget);

    // Store the attachment handle
    this->attachmentHandles[std::string(name)] = handle;

    // Update the render pass description
    this->desc.addDepthAttachment(name);
    return handle;
  }


}  // namespace ren