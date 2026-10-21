#pragma once
#include <ren/renderer/graph/RenderGraph.h>
#include <imgui/imgui.h>
#include <deque>
namespace ren {
  class Texture;


  // ImGuiImages lets ImGui show graph images. The UI is laid out before the
  // graph prepares the frame, so the image of a handle is not known when
  // ImGui::Image() is called. ImGuiImages defers the lookup:
  //
  //   1. Layout: request() makes an entry with an ImTextureData that has no
  //      texture ID yet, and returns an ImTextureRef that points to it. ImGui
  //      keeps this pointer in its draw commands.
  //   2. After RenderGraph::prepareFrame(): resolve() gets the prepared image
  //      of each entry with RenderGraph::displayImage(), and writes the ImGui
  //      descriptor of that image into the entry with SetTexID().
  //   3. Recording: the ImGui Vulkan backend calls ImDrawCmd::GetTexID(),
  //      which reads the descriptor from the entry.
  //
  // The entries are in a std::deque, so their addresses stay the same until
  // the next beginFrame(). This includes the draws of detached ImGui windows,
  // which record at the end of the frame. The entries are not registered with
  // ImGui, so ImGui does not upload or destroy them.
  //
  // The graph owns the images. ImGuiImages owns the ImGui descriptors: one per
  // storage, shared by all entries that show that storage. The frame keeps
  // each descriptor and its image until the GPU completes the frame.
  class ImGuiImages {
   public:
    explicit ImGuiImages(RenderGraph &graph)
        : graph(graph) {}


    // Return a texture for ImGui::Image() that shows the image of a handle in
    // this frame. Call it during layout, before Renderer::recordGraph(). This
    // does not make the producer of the image run. History that is not
    // available shows a black pixel. An optional image without storage (e.g.
    // of a task that never ran) also shows the black pixel; a required one
    // makes resolve() throw.
    ImTextureRef request(GraphHandle image, bool optional = false);

    // Write the descriptor of each requested image into its entry. Call after
    // RenderGraph::prepareFrame() and before RenderGraph::record(), because
    // displayImage() adds the final shader-read barriers. Throws if a current
    // image has no storage.
    void resolve(SubmissionUnit &);

    // Remove the entries of the last frame, and forget descriptors that no
    // frame uses.
    void beginFrame();

    // Remove all entries and the placeholder. Frames in flight keep the
    // descriptors that they use.
    void clear();


   private:
    // The ImGui descriptor of one storage. The destructor removes it, after
    // the last frame that uses it completes.
    struct Binding {
      ref<ImageStorage> storage;
      VkDescriptorSet descriptor = VK_NULL_HANDLE;
      ~Binding();
    };
    // One request() call. texture.TexID is invalid until resolve().
    struct Entry {
      GraphHandle handle;
      bool optional = false;
      ImTextureData texture;
    };
    RenderGraph &graph;
    std::deque<Entry> entries;  // A deque does not move its elements when it grows.

    // Storage to binding. The map keeps weak references; the frames that use
    // a binding keep it alive.
    std::unordered_map<ImageStorage *, std::weak_ptr<Binding>> bindings;

    ref<Texture> placeholder;  // Shown for history that is not available.
  };
}  // namespace ren
