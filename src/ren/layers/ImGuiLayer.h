#pragma once


#include <ren/types.h>
#include <ren/layers/Layer.h>

namespace ren {
  class ImGuiLayer : public Layer {
    VkDescriptorPool imguiPool = VK_NULL_HANDLE;  // Descriptor pool for ImGui

   public:
    ImGuiLayer(Application &app);
    ~ImGuiLayer() override = default;


    void onAttach(void) override;
    void onDetach(void) override;
    void onEvent(Event &event) override;
    void onImguiRender(float deltaTime) override;
  };
}  // namespace ren