#include "EditorWindow.h"

#include <ren/core/Application.h>
#include <ren/core/DebugLines.hpp>
#include <ren/core/Logging.h>
#include <ren/misc/resource_usage.h>
#include <ren/renderer/CommandEncoder.h>
#include <ren/renderer/Renderer.h>
#include <ren/Camera.h>

#include <imgui.h>
#include <ImGuizmo/ImGuizmo.h>

namespace editor {


  void EditorWindow::onInitialize(void) {
    m_viewport = ren::makeBox<ren::EditorViewport>();
    m_viewport->position = {0.0f, 1.5f, 4.5f};
    m_viewport->angles = {-0.08f, 0.0f, 0.0f};
    m_viewport->showInputOverlay = true;
  }


  void EditorWindow::onUpdate(float dt) {
    m_deltaTime = dt;

    // TOOD: Move this to some kind of camera system.  The problem is that we
    // need the frame globals to be set before we can render, and the camera
    // is part of the frame globals.
    auto &cam = ren::Camera::get();
    cam.update(dt);

    auto &frame = ren::getFrameUnit();
    const auto &backbuffer = getSwapchain().currentImage();
    const glm::vec2 frameSize{backbuffer->getWidth(), backbuffer->getHeight()};
    m_viewMatrix = cam.view_matrix();
    m_projMatrix = ren::Camera::projectionMatrix(frameSize.x, frameSize.y);
    frame.setFrameGlobals({
        .time = ren::timeSeconds(),
        .deltaTime = dt,
        .frameNumber = static_cast<u32>(ren::getVulkan().frame_number),
        .renderSize = frameSize,
        .inverseRenderSize = 1.0f / frameSize,
        .viewMatrix = m_viewMatrix,
        .projMatrix = m_projMatrix,
    });

    m_framerateCounter.addFrame(dt);

    // Each EditorViewport sets its own ImGuizmo draw list and rect.
    ImGuizmo::BeginFrame();
    // Before rendering, lets create a dockspace
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

    if (ImGui::BeginMainMenuBar()) {
      if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("ImGui Demo")) {
          ren::logUI("ImGui Demo", [](auto &ctx) { ImGui::ShowDemoWindow(&ctx.opened); });
        }
        ImGui::EndMenu();
      }

      // Sum usage and budget over the device-local heaps. vmaGetHeapBudgets writes one entry per heap.
      VmaAllocator allocator = ren::getVulkan().allocator;
      const VkPhysicalDeviceMemoryProperties *memProps = nullptr;
      vmaGetMemoryProperties(allocator, &memProps);
      VmaBudget budgets[VK_MAX_MEMORY_HEAPS];
      vmaGetHeapBudgets(allocator, budgets);
      VkDeviceSize vramUsage = 0;
      VkDeviceSize vramBudget = 0;
      for (u32 i = 0; i < memProps->memoryHeapCount; i++) {
        vramUsage += budgets[i].usage;
        vramBudget += budgets[i].budget;
      }

      const u64 hostAnonymous = ren::getCurrentProcessAnonymousRSS();

      // Simply print the framerate in the menu bar.
      char buf[192];
      snprintf(buf, sizeof(buf), "%4d FPS DT=%.3f   |   VRAM: %.2f/%.2f MB (%7.1f%%)   |   Host: %.2f MB",
               (int)m_framerateCounter.getAverageFramerate(), dt, vramUsage / 1024.0 / 1024.0, vramBudget / 1024.0 / 1024.0,
               (vramUsage * 100.0) / vramBudget, hostAnonymous / 1024.0 / 1024.0);
      ImGui::MenuItem(buf);

      ImGui::EndMainMenuBar();
    }

    ren::inspectLog();

    ImGui::Begin("ShaderCache");
    ren::Renderer::get().getShaderCache().inspect();
    ImGui::End();

    auto &world = ren::world();
    REN_PROFILE_EXPR("World Tick", world.progress(dt));
  }


  void EditorWindow::onRender(ren::FrameContext &frame) {
    auto &renderer = ren::Renderer::get();

    m_viewport->drawWindow("Viewport A", m_deltaTime);
    m_viewport->render(renderer);
    m_viewport->resolveImGuiImage();

    auto encoder = frame.getMainCommandEncoder();
    auto timestampTicket = encoder->beginTimestampQuery("FullScreenPass");
    auto pass = encoder->beginRenderPass(*renderer.getDisplayPass(), getSwapchain().currentTarget());
    {
      REN_PROFILE_SCOPE("FullScreenPass");
      ren::renderDebugLines(m_viewMatrix, m_projMatrix);
      drawImGui(pass);
    }
    pass.end();
    encoder->endTimestampQuery(timestampTicket);
  }

}  // namespace editor
