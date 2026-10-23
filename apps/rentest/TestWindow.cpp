#include "TestWindow.h"

#include <ren/core/Application.h>
#include <ren/core/Logging.h>
#include <ren/renderer/CommandEncoder.h>
#include <ren/renderer/Renderer.h>
#include <ren/renderer/shader/ShaderCache.h>
#include <ren/renderer/submission/FrameContext.h>
#include <ren/renderer/vulkan/Vulkan.h>

#include <imgui/imgui.h>

#include <algorithm>

namespace rentest {

  static const ImVec4 kPassColor{0.40f, 0.85f, 0.40f, 1.0f};
  static const ImVec4 kFailColor{1.00f, 0.40f, 0.40f, 1.0f};
  static const ImVec4 kSkipColor{0.85f, 0.75f, 0.30f, 1.0f};

  // The overlay text is larger than the default ImGui text, so that it is easy
  // to read while a person walks through the tests.
  static const float kOverlayFontScale = 1.3f;


  TestWindow::TestWindow(const TestInfo &info, TestResult &result, const RunSettings &settings, size_t index, size_t count)
      : info(info)
      , result(result)
      , test(info.make())
      , ctx(info.config, result)
      , settings(settings)
      , index(index)
      , count(count)
      , validationStart(ren::getValidationErrorCount()) {}



  ////////////////////////////////////////////////////////////////////////////
  // Window hooks
  ////////////////////////////////////////////////////////////////////////////

  void TestWindow::onInitialize(void) {
    // Each test makes a new ImGui context. Without this, each one writes its
    // layout over the imgui.ini of the editor.
    ImGui::GetIO().IniFilename = nullptr;

    // The pipeline that draws the shown image into the window.
    displayPso.debugName = "tests/display";
    displayPso.program = ren::getShader("tests/display", "ren/pass/fullscreen");
    displayPso.hasVertexBinding = false;
    displayPso.cullMode = ren::CullMode::None;
    displayPso.depthTest = false;
    displayPso.depthWrite = false;

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

    // After the test finishes, the viewports do not update, so their graphs do
    // not run. The window then shows the final image that the checks and the
    // golden comparison used. A temporal image would change each frame. The
    // graph keeps the last storage of each resource, so the image and the
    // inspector stay available.
    if (!finished) {
      for (auto &viewport : ctx.viewports) {
        guard("Viewport::update", [&]() { viewport->update(dt, ctx.renderSize); });
      }
    }

    // ren::deltaTime() and the ECS systems use the world time.
    ren::world().progress(dt);

    drawInspector();
    drawOverlay();
    handleKeys();
  }


  void TestWindow::onRender(ren::FrameContext &frame) {
    auto &renderer = ren::Renderer::get();

    // Window::render() catches the error and discards the frame. Record it
    // first, so that the test fails.
    output.reset();
    try {
      renderer.prepareGraph();

      // displayImage() moves the output to a shader-read layout at the end of
      // the graph work, and the frame keeps it until the GPU completes. The
      // graph resolves a handle only in a prepared frame, so the window also
      // keeps the storage for the read back after the last frame.
      auto *shown = ctx.getShownViewport();
      if (shown && shown->output()) {
        output = renderer.getGraph().displayImage(shown->output());
      }

      renderer.recordGraph();
    } catch (const std::exception &error) {
      recordError(fmt::format("render: {}", error.what()));
      throw;
    }

    auto pass = frame.getMainCommandEncoder()->beginRenderPass(*renderer.getDisplayPass(), getSwapchain().currentTarget());
    drawOutput(pass);
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
    // onFinish() can check pixels of the final image. The golden comparison
    // comes last, so that a failed check also stops --save-golden.
    checkValidation();
    if (!result.skipped()) {
      guard("readback", [&]() { readFinalImage(); });
      guard("onFinish", [&]() { test->onFinish(ctx); });
      guard("golden", [&]() { checkGolden(); });
    }

    if (!settings.interactive) {
      ren::Application::get().stop();
    }
  }


  void TestWindow::readFinalImage(void) {
    // A failed frame can leave the image in a different layout.
    if (!output || !result.failures.empty()) {
      return;
    }

    // Each frame ends with the output in the shader-read layout, because the
    // window shows it with ImGuiImages.
    const auto &image = *output->image;
    const auto bytes = image.readPixels(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    finalImage = toPixelImage(image.getFormat(), image.getWidth(), image.getHeight(), bytes, info.config.floatEncoding);
    ctx.finalImage = &*finalImage;

    // In interactive mode, the window shows this image after the test, so
    // that a person sees exactly what the checks used, also for a float image.
    if (settings.interactive) {
      auto pixels = finalImage->pixels;
      resultTexture = ren::make<ren::Texture>("result", finalImage->width, finalImage->height, pixels.data());
    }
  }


  bool TestWindow::canSaveGolden(void) const { return settings.saveGolden && finalImage && result.failures.empty(); }


  void TestWindow::checkGolden(void) {
    if (!finalImage) {
      return;
    }

    const auto path = goldenPath(info.name);
    const auto expected = loadPng(path);
    const bool sameSize = expected && expected->width == finalImage->width && expected->height == finalImage->height;
    if (sameSize) {
      result.golden = compareImages(*finalImage, *expected);
    }

    // In interactive mode, G shows the golden image and the diff in the
    // window. The textures are UNORM, like the test images, so tests/display
    // shows their bytes the same way.
    if (settings.interactive && expected) {
      auto pixels = expected->pixels;
      goldenTexture = ren::make<ren::Texture>("golden", expected->width, expected->height, pixels.data());
      if (sameSize) {
        auto diff = makeDiffImage(*finalImage, *expected);
        diffTexture = ren::make<ren::Texture>("golden diff", diff.width, diff.height, diff.pixels.data());
      }
    }

    // With --save-golden, a missing or different golden image is not a
    // failure. The comparison still shows what the new image changes.
    if (settings.saveGolden) {
      if (!settings.interactive && canSaveGolden()) {
        saveGolden();
      }
      return;
    }

    if (!expected) {
      ctx.fail(fmt::format("golden: no golden image at {}; run with --save-golden", path.string()));
      return;
    }
    if (!sameSize) {
      ctx.fail(fmt::format("golden: the result is {}x{}, but the golden image is {}x{}", finalImage->width, finalImage->height, expected->width,
                           expected->height));
      return;
    }

    if (result.golden->meanError > info.config.goldenTolerance) {
      const auto diffPath = outputPath(info.name, "diff");
      writePng(outputPath(info.name, "actual"), *finalImage);
      writePng(diffPath, makeDiffImage(*finalImage, *expected));
      ctx.fail(fmt::format("golden: mean error {:.6f} is over the tolerance {:.6f}. See {}", result.golden->meanError, info.config.goldenTolerance,
                           diffPath.string()));
    }
  }


  void TestWindow::saveGolden(void) {
    writePng(goldenPath(info.name), *finalImage);
    result.goldenSaved = true;
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


  void TestWindow::checkValidation(void) {
    const u64 errors = ren::getValidationErrorCount() - validationStart;
    if (errors == 0) {
      return;
    }

    result.validationErrors = errors;
    recordError(fmt::format("{} Vulkan validation error(s). The last: {}", errors, ren::getLastValidationError()));
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

  void TestWindow::drawOutput(ren::RenderPassEncoder &pass) {
    // The output is null if the graph of the viewport failed to build, or if
    // the frame failed before displayImage().
    ren::ImageRef source = output ? output->image : nullptr;
    if (resultTexture) {
      source = resultTexture->getImage();
    }
    if (view == View::Golden && goldenTexture) {
      source = goldenTexture->getImage();
    } else if (view == View::Diff && diffTexture) {
      source = diffTexture->getImage();
    }
    if (!source) {
      return;
    }

    // As large as it fits in the window, with the aspect ratio of the image,
    // and centered. The rectangle is in window uv, so the pixel density of
    // the display does not matter.
    const glm::vec2 window(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
    const glm::vec2 image(source->getWidth(), source->getHeight());
    const glm::vec2 extent = image * std::min(window.x / image.x, window.y / image.y);
    const glm::vec2 origin = (window - extent) * 0.5f;
    const glm::vec4 rect(origin / window, (origin + extent) / window);

    // Nearest filtering shows each pixel of a small test image as a sharp
    // block. A UNORM image decodes in the shader; see tests/display.slang.
    auto &sampler = ren::Renderer::get().getSampler(VK_FILTER_NEAREST);
    const auto format = source->getFormat();
    const u32 decodeSrgb = (format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_B8G8R8A8_UNORM) ? 1 : 0;

    auto shader = pass.bindGraphics(displayPso);
    auto root = shader.root("root");
    root.set("image", ren::TextureHandle::pack(source->sampledIndex(), sampler.index()));
    root.set("rect", rect);
    root.set("decodeSrgb", decodeSrgb);
    shader.drawFullscreenTriangle();
  }


  void TestWindow::drawInspector(void) {
    auto *shown = ctx.getShownViewport();
    if (!shown || !showInspector) {
      return;
    }

    ImGui::Begin("Graph Inspector", &showInspector);
    shown->inspect();
    ImGui::End();
  }


  void TestWindow::drawOverlay(void) {
    const auto flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
                       ImGuiWindowFlags_NoTitleBar;
    ImGui::SetNextWindowPos(ImVec2(8, 8), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.75f);
    ImGui::PushFontSize(ImGui::GetStyle().FontSizeBase * kOverlayFontScale);
    ImGui::Begin("Test", nullptr, flags);
    ImGui::PushTextWrapPos(360.0f * kOverlayFontScale);

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
    if (result.golden) {
      const auto &golden = *result.golden;
      ImGui::TextDisabled("Golden: mean %.6f  max %.3f  (%zu of %zu pixels differ)", golden.meanError, golden.maxError, golden.differentPixels,
                          golden.pixelCount);
    }
    if (goldenTexture) {
      static const char *names[] = {"result", "golden", "diff (x8)"};
      ImGui::TextColored(view == View::Result ? kPassColor : kSkipColor, "Showing: %s  (G to change)", names[int(view)]);
    }

    // The manual verdict.
    if (settings.interactive && finished) {
      if (canSaveGolden()) {
        ImGui::TextColored(kSkipColor, "Pass saves this image as the golden image.");
      }
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
    ImGui::PopFontSize();
  }


  void TestWindow::handleKeys(void) {
    // Keys are text while a person types the note.
    if (ImGui::GetIO().WantTextInput) {
      return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_I, false)) {
      showInspector = !showInspector;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_G, false)) {
      nextView();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
      result.aborted = true;
      ren::Application::get().stop();
    }

    // A verdict waits for the automatic checks.
    if (!settings.interactive || !finished) {
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


  void TestWindow::nextView(void) {
    // Skip the views that have no image, e.g. the diff of a golden image of a
    // different size.
    for (int i = 0; i < 3; i++) {
      view = View((int(view) + 1) % 3);
      if (view == View::Result || (view == View::Golden && goldenTexture) || (view == View::Diff && diffTexture)) {
        return;
      }
    }
  }


  void TestWindow::setVerdict(TestResult::Verdict verdict) {
    // In interactive mode, the pass verdict is the review of a new golden
    // image.
    if (verdict == TestResult::Verdict::Pass && canSaveGolden()) {
      guard("golden", [&]() { saveGolden(); });
    }

    result.manual = verdict;
    result.note = note.data();
    ren::Application::get().stop();
  }

}  // namespace rentest
