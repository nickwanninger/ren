#include <ren/renderer/graph/ImGuiImages.h>
#include <ren/renderer/Texture.h>
#include <imgui/backends/imgui_impl_vulkan.h>
namespace ren {
  ImGuiImages::Binding::~Binding() {
    if (descriptor) {
      ImGui_ImplVulkan_RemoveTexture(descriptor);
    }
  }


  ImTextureRef ImGuiImages::request(GraphHandle handle, bool optional) {
    entries.emplace_back();
    auto &entry = entries.back();
    entry.handle = handle;
    entry.optional = optional;
    entry.texture.SetTexID(ImTextureID_Invalid);
    return entry.texture.GetTexRef();
  }


  void ImGuiImages::resolve(SubmissionUnit &submission) {
    for (auto &entry : entries) {
      // An optional image without storage skips displayImage(), which throws for it.
      ref<ImageStorage> image;
      if (!entry.optional || graph.resolve(entry.handle)) {
        image = graph.displayImage(entry.handle);
      }

      if (!image) {
        // History that is not available shows a black pixel.
        if (!placeholder) {
          placeholder = Texture::createSinglePixel("Unavailable graph history", 0, 0, 0, 255);
        }
        entry.texture.SetTexID((ImTextureID)placeholder->getImGui());
        submission.retain(placeholder);
        continue;
      }

      // One descriptor per storage. New storage, for example after a resize,
      // gets a new descriptor.
      auto binding = bindings[image.get()].lock();
      if (!binding) {
        binding = make<Binding>();
        binding->storage = image;
        binding->descriptor =
            ImGui_ImplVulkan_AddTexture(image->sampler->getHandle(), image->image->getImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        bindings[image.get()] = binding;
      }
      entry.texture.SetTexID((ImTextureID)binding->descriptor);

      // The frame keeps the descriptor and its image until the GPU completes it.
      submission.retain(binding);
    }
  }


  void ImGuiImages::beginFrame() {
    entries.clear();
    std::erase_if(bindings, [](const auto &entry) { return entry.second.expired(); });
  }


  void ImGuiImages::clear() {
    entries.clear();
    bindings.clear();
    placeholder.reset();
  }
}  // namespace ren
