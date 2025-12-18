#include <ren/core/FramerateCounter.h>
#include <imgui/imgui.h>

namespace ren {

  // TODO: outline!
  static float lerp(float a, float b, float t) { return a + t * (b - a); }
  void FramerateCounter::inspect(void) {
    ImGui::Text("Average Delta Time: %.4f ms", getAverageDeltaTime() * 1000.0f);
    ImGui::Text("Average Framerate: %.2f FPS", getAverageFramerate());

    ImGui::PushStyleColor(ImGuiCol_PlotLines, IM_COL32(255, 255, 0, 255));


    float max = 0.0f;
    for (size_t i = 0; i < FRAMERATE_TRACKER_SIZE; ++i) {
      max = std::max(max, deltaTimes[i]);
    }
    this->lerpMax = lerp(this->lerpMax, max, 0.1f);

    ImGui::PlotLines(
        "Frame Time (ms)", [](void* data, int idx) { return ((float*)data)[idx] * 1000.0f; },
        deltaTimes, FRAMERATE_TRACKER_SIZE, head, nullptr, 0.0f, this->lerpMax * 1000.0f,
        ImVec2(0, 80));

    // ImGui::PlotLines("Frame Time (ms)", deltaTimes, FRAMERATE_TRACKER_SIZE, head, "Frame
    // Time", 0.0f,
    //                  this->maxSeen, ImVec2(0, 80));
    if (ImGui::Button("Reset")) { reset(); }
    ImGui::PopStyleColor();
  }
}  // namespace ren