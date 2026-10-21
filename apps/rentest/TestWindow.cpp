#include "TestWindow.h"

#include <ren/core/Application.h>
#include <ren/core/Logging.h>
#include <ren/renderer/CommandEncoder.h>
#include <ren/renderer/Renderer.h>
#include <ren/renderer/submission/FrameContext.h>

#include <imgui/imgui.h>

#include <algorithm>

namespace rentest {

  static const ImVec4 kPassColor{0.40f, 0.85f, 0.40f, 1.0f};
  static const ImVec4 kFailColor{1.00f, 0.40f, 0.40f, 1.0f};
  static const ImVec4 kSkipColor{0.85f, 0.75f, 0.30f, 1.0f};


  TestWindow::TestWindow(const TestInfo &info, TestResult &result, bool interactive, size_t index, size_t count)
      : info(info)
      , result(result)
      , test(info.make())
      , ctx(info.config, result)
      , interactive(interactive)
      , index(index)
      , count(count) {}



  ////////////////////////////////////////////////////////////////////////////
  // Window hooks
  ////////////////////////////////////////////////////////////////////////////

  void TestWindow::onInitialize(void) {
    // Each test makes a new ImGui context. Without this, each one writes its
    // layout over the imgui.ini of the editor.
    ImGui::GetIO().IniFilename = nullptr;

    guard("onSetup", [&]() { test->onSetup(ctx); });
  }


  void TestWindow::onEvent(const SDL_Event &event) {
    // The Application stops on this event. Stop the whole run, not only this
    // test.
    if (event.type == SDL_EVENT_QUIT) {
      result.aborted = true;
    }
  }


  void TestWindow::onUpdate(float dt) {
    if (!finished) {
      stepTest();
    }

    // In interactive mode, the viewports continue to render after the test
    // finishes, so that a person can look at the result.
    for (auto &viewport : ctx.viewports) {
      guard("Viewport::update", [&]() { viewport->update(dt, info.config.size); });
    }

    // ren::deltaTime() and the ECS systems use the world time.
    ren::world().progress(dt);

    drawViewport();
    drawOverlay();
    handleKeys();
  }


  void TestWindow::onRender(ren::FrameContext &frame) {
    auto &renderer = ren::Renderer::get();

    // Window::render() catches the error and discards the frame. Record it
    // first, so that the test fails.
    try {
      renderer.prepareGraph();
      renderer.recordGraph();
    } catch (const std::exception &error) {
      recordError(fmt::format("render: {}", error.what()));
      throw;
    }

    auto pass = frame.getMainCommandEncoder()->beginRenderPass(*renderer.getDisplayPass(), getSwapchain().currentTarget());
    drawImGui(pass);
    pass.end();
  }



  ////////////////////////////////////////////////////////////////////////////
  // Test steps
  ////////////////////////////////////////////////////////////////////////////

  void TestWindow::stepTest(void) {
    if (ctx.finished || ctx.frame >= info.config.frames) {
      finishTest();
      return;
    }

    guard("onFrame", [&]() { test->onFrame(ctx, ctx.frame); });
    ctx.frame++;
  }


  void TestWindow::finishTest(void) {
    finished = true;
    ctx.finished = true;

    // The checks can read the results of all frames that the test rendered.
    ren::Renderer::get().waitForIdle();
    if (!result.skipped()) {
      guard("onFinish", [&]() { test->onFinish(ctx); });
    }

    if (!interactive) {
      ren::Application::get().stop();
    }
  }


  template <class Fn>
  void TestWindow::guard(const char *hook, Fn &&fn) {
    try {
      fn();
    } catch (const std::exception &error) {
      recordError(fmt::format("{}: {}", hook, error.what()));
      ctx.finished = true;
    }
  }


  void TestWindow::recordError(const std::string &message) {
    auto &failures = result.failures;
    if (std::find(failures.begin(), failures.end(), message) == failures.end()) {
      failures.push_back(message);
    }
  }



  ////////////////////////////////////////////////////////////////////////////
  // Display and overlay
  ////////////////////////////////////////////////////////////////////////////

  void TestWindow::drawViewport(void) {
    if (ctx.viewports.empty()) {
      return;
    }

    // The image is drawn behind all ImGui windows, over the whole window.
    // ImGui stretches it if the window size differs from the render size. The
    // output is null if the graph of the viewport failed to build.
    auto output = ctx.viewports.front()->output();
    if (output) {
      auto texture = ren::Renderer::get().getGraphImages().request(output);
      ImGui::GetBackgroundDrawList()->AddImage(texture, ImVec2(0, 0), ImGui::GetIO().DisplaySize);
    }

    if (showInspector) {
      ImGui::Begin("Graph Inspector", &showInspector);
      ctx.viewports.front()->inspect();
      ImGui::End();
    }
  }


  void TestWindow::drawOverlay(void) {
    const auto flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
                       ImGuiWindowFlags_NoTitleBar;
    ImGui::SetNextWindowPos(ImVec2(8, 8), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.75f);
    ImGui::Begin("Test", nullptr, flags);
    ImGui::PushTextWrapPos(360.0f);

    ImGui::Text("[%zu/%zu] %s", index + 1, count, info.name.c_str());
    ImGui::TextDisabled("frame %u/%u", ctx.frame, info.config.frames);
    if (info.config.expect[0] != '\0') {
      ImGui::Separator();
      ImGui::TextWrapped("Expect: %s", info.config.expect);
    }

    // The automatic result.
    ImGui::Separator();
    if (!finished) {
      ImGui::TextDisabled("Running...");
    } else if (result.skipped()) {
      ImGui::TextColored(kSkipColor, "Skipped: %s", result.skipReason.c_str());
    } else if (result.failures.empty()) {
      ImGui::TextColored(kPassColor, "Automatic checks passed");
    } else {
      ImGui::TextColored(kFailColor, "Automatic checks failed:");
    }
    for (auto &failure : result.failures) {
      ImGui::BulletText("%s", failure.c_str());
    }

    // The manual verdict.
    if (interactive && finished) {
      ImGui::Separator();
      ImGui::SetNextItemWidth(-1);
      ImGui::InputTextWithHint("##note", "Note (optional)", note.data(), note.size());

      if (ImGui::Button("Pass (P)")) {
        setVerdict(TestResult::Verdict::Pass);
      }
      ImGui::SameLine();
      if (ImGui::Button("Fail (F)")) {
        setVerdict(TestResult::Verdict::Fail);
      }
      ImGui::SameLine();
      if (ImGui::Button("Skip (S)")) {
        setVerdict(TestResult::Verdict::Skip);
      }
      ImGui::SameLine();
      if (ImGui::Button("Rerun (R)")) {
        result.rerun = true;
        ren::Application::get().stop();
      }
      ImGui::TextDisabled("I: graph inspector   Esc: stop the run");
    }

    ImGui::PopTextWrapPos();
    ImGui::End();
  }


  void TestWindow::handleKeys(void) {
    // Keys are text while a person types the note.
    if (ImGui::GetIO().WantTextInput) {
      return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_I, false)) {
      showInspector = !showInspector;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
      result.aborted = true;
      ren::Application::get().stop();
    }

    // A verdict waits for the automatic checks.
    if (!interactive || !finished) {
      return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_P, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
      setVerdict(TestResult::Verdict::Pass);
    } else if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
      setVerdict(TestResult::Verdict::Fail);
    } else if (ImGui::IsKeyPressed(ImGuiKey_S, false)) {
      setVerdict(TestResult::Verdict::Skip);
    } else if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
      result.rerun = true;
      ren::Application::get().stop();
    }
  }


  void TestWindow::setVerdict(TestResult::Verdict verdict) {
    result.manual = verdict;
    result.note = note.data();
    ren::Application::get().stop();
  }

}  // namespace rentest
