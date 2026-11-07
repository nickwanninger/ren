

#include <ren/renderer/Renderer.h>
#include <ren/core/windows/ImGuiWindow.h>

namespace ren {

  void ImGuiWindow::onUpdate(float dt) {
    // Make a fullscreen imgui window.
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    // ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    ImGui::Begin("Fullscreen", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings);

    draw();

    ImGui::End();
    ImGui::PopStyleVar(2);
  }

  void ImGuiWindow::onRender(FrameContext &frame) {
    auto &renderer = ren::Renderer::get();

    auto encoder = frame.getMainCommandEncoder();
    auto timestampTicket = encoder->beginTimestampQuery("FullScreenPass");
    auto pass = encoder->beginRenderPass(*renderer.getDisplayPass(), getSwapchain().currentTarget());
    drawImGui(pass);
    pass.end();
    encoder->endTimestampQuery(timestampTicket);
  }
}  // namespace ren