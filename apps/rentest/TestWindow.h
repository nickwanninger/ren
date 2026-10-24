#pragma once

#include "RenderTest.h"

#include <ren/core/Window.h>
#include <ren/renderer/graph/ImageResource.h>
#include <ren/renderer/Texture.h>
#include <ren/renderer/pipelines/PipelineStateObject.h>

#include <array>

namespace rentest {

  // The settings of the whole run, from the command line.
  struct RunSettings {
    bool interactive = false;  // Wait for a verdict from a person after each test.
    bool saveGolden = false;   // Write the final image of each test as its golden image.
  };


  // The window of one test. It runs the hooks of the test, updates the
  // viewports of the test, and shows one viewport (see TestContext::show())
  // over the whole window with an overlay on top.
  //
  // In automatic mode, the window stops the Application after onFinish(). In
  // interactive mode, the window shows the final image until a person gives a
  // verdict with a key or a button in the overlay. The viewports do not update
  // after the test finishes, so the image stays the one that was checked.
  //
  // Before onFinish(), the window reads the final image of the shown viewport
  // back to the CPU, and compares it with the golden image of the test. With
  // --save-golden, it writes the final image as the new golden image: in
  // automatic mode at once, and in interactive mode only on a pass verdict.
  class TestWindow : public ren::Window {
   public:
    // index and count are the position of this test in the run, for the
    // overlay. result must stay valid until the Application is destroyed.
    TestWindow(const TestInfo &info, TestResult &result, const RunSettings &settings, size_t index, size_t count);

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
    // Read the final image of the shown viewport back to the CPU. Skipped if
    // the test has no viewport or already failed.
    void readFinalImage(void);
    // Compare the final image with the golden image, or save it as the golden
    // image. A test with failures never saves.
    void checkGolden(void);
    bool canSaveGolden(void) const;
    void saveGolden(void);

    // Fail the test if the Vulkan validation layers reported errors since the
    // window was made.
    void checkValidation(void);

    // Record a failure one time. A render error can repeat each frame.
    void recordError(const std::string &message);

    // Draw the output into the window pass, under ImGui.
    void drawOutput(ren::RenderPassEncoder &pass);
    void drawInspector(void);
    void drawOverlay(void);
    void handleKeys(void);
    // Show the next of the result, the golden image, and the diff.
    void nextView(void);
    // Save the verdict and the note, then stop the Application.
    void setVerdict(TestResult::Verdict verdict);


    const TestInfo &info;
    TestResult &result;
    ren::Box<RenderTest> test;
    TestContext ctx;

    RunSettings settings;
    size_t index = 0, count = 0;

    // The output image of the shown viewport in the last prepared frame. The
    // graph cannot resolve a handle after the frame, so the window keeps it.
    ren::ref<ren::ImageStorage> output;
    std::optional<PixelImage> finalImage;  // The read back of output. ctx points to it.
    ren::PipelineStateObject displayPso;   // Draws output into the window. See drawOutput().

    // What the window shows. The golden and diff textures exist only in
    // interactive mode, after a comparison with a golden image.
    enum class View { Result, Golden, Diff };
    View view = View::Result;
    ren::ref<ren::Texture> goldenTexture, diffTexture;
    ren::ref<ren::Texture> resultTexture;  // finalImage, in interactive mode.

    bool finished = false;    // onFinish() ran.
    u64 validationStart = 0;  // The validation error count when the window was made.
    bool showInspector = false;
    std::array<char, 256> note{};
  };

}  // namespace rentest
