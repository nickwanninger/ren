#pragma once

#include "RenderTest.h"

#include <ren/core/Window.h>

#include <array>

namespace rentest {

  // The window of one test. It runs the hooks of the test, updates the
  // viewports of the test, and shows the first viewport over the whole window
  // with an overlay on top.
  //
  // In automatic mode, the window stops the Application after onFinish(). In
  // interactive mode, the viewports continue to render until a person gives a
  // verdict with a key or a button in the overlay.
  class TestWindow : public ren::Window {
   public:
    // index and count are the position of this test in the run, for the
    // overlay. result must stay valid until the Application is destroyed.
    TestWindow(const TestInfo &info, TestResult &result, bool interactive, size_t index, size_t count);

   protected:
    void onInitialize(void) override;
    void onEvent(const SDL_Event &event) override;
    void onUpdate(float dt) override;
    void onRender(ren::FrameContext &frame) override;

   private:
    // Call onFrame() for the next frame, or call onFinish() after the last.
    void stepTest(void);
    // Wait for the GPU, call onFinish(), and stop the Application in
    // automatic mode.
    void finishTest(void);

    // Run a hook of the test. An exception fails the test, and no more frames
    // call onFrame().
    template <class Fn>
    void guard(const char *hook, Fn &&fn);
    // Record a failure one time. A render error can repeat each frame.
    void recordError(const std::string &message);

    void drawViewport(void);
    void drawOverlay(void);
    void handleKeys(void);
    // Save the verdict and the note, then stop the Application.
    void setVerdict(TestResult::Verdict verdict);


    const TestInfo &info;
    TestResult &result;
    ren::Box<RenderTest> test;
    TestContext ctx;

    bool interactive = false;
    size_t index = 0, count = 0;

    bool finished = false;  // onFinish() ran.
    bool showInspector = false;
    std::array<char, 256> note{};
  };

}  // namespace rentest
