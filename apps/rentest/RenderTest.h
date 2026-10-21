#pragma once

#include <ren/types.h>
#include <ren/renderer/Viewport.h>

#include <functional>
#include <string>
#include <vector>

namespace rentest {

  class TestContext;


  // The settings of one test. Give them in REN_RENDER_TEST(), e.g.
  //   REN_RENDER_TEST("shader.uv", UVTest, {.size = {256, 256}, .frames = 3});
  struct TestConfig {
    // The render size of each viewport, and the size of the window.
    glm::uvec2 size{1280, 720};
    // The number of frames that call RenderTest::onFrame(). The automatic
    // checks run after the last of these frames.
    u32 frames = 3;
    // The time step of each frame, in seconds. The wall clock is not used.
    float deltaTime = 1.0f / 60.0f;
    // What a person must see in the window if the test passes. The
    // interactive mode shows it.
    const char *expect = "";
  };


  // One rendering test. The harness makes a new Application for each test,
  // and calls the hooks in this order:
  //   1. onSetup(): one time, after the device and the Renderer exist. Make
  //      the viewports and the scene here.
  //   2. onFrame(): one time for each of TestConfig::frames frames, before
  //      the viewports update.
  //   3. onFinish(): one time, after the GPU is idle. Do the checks here.
  // An exception from a hook fails the test. In interactive mode, the
  // viewports continue to render after onFinish() until a person gives a
  // verdict.
  class RenderTest {
   public:
    virtual ~RenderTest() = default;

    virtual void onSetup(TestContext &ctx) {}
    virtual void onFrame(TestContext &ctx, u32 frame) {}
    virtual void onFinish(TestContext &ctx) {}
  };


  // The automatic and manual result of one test.
  struct TestResult {
    enum class Verdict { None, Pass, Fail, Skip };

    std::vector<std::string> failures;  // Empty if the automatic checks passed.
    std::string skipReason;             // Not empty if the test skipped itself.

    Verdict manual = Verdict::None;  // Set only in interactive mode.
    std::string note;                // The note of the person, if any.

    bool rerun = false;    // The person asked to run the test again.
    bool aborted = false;  // The person closed the window. The run stops.

    bool skipped() const { return !skipReason.empty(); }
    Verdict automatic() const;
    // The manual verdict if there is one, else the automatic verdict.
    Verdict verdict() const;
  };

  const char *verdictName(TestResult::Verdict verdict);


  // The interface that a test uses to talk to the harness. The harness owns
  // the viewports that the test makes, and updates them each frame.
  class TestContext {
   public:
    TestContext(const TestConfig &config, TestResult &result)
        : config(config)
        , result(result) {}

    // Make a viewport of type T, and update it each frame at the render size
    // of the test. The window shows the first viewport.
    template <class T, class... Args>
    T &makeViewport(Args &&...args) {
      auto viewport = ren::makeBox<T>(std::forward<Args>(args)...);
      T &out = *viewport;
      viewports.push_back(std::move(viewport));
      return out;
    }

    // Record a failure. The test continues, so that one run reports all of
    // its failures.
    void fail(std::string message) { result.failures.push_back(std::move(message)); }

    // Record a failure if value is false.
    void check(bool value, std::string message) {
      if (!value) {
        fail(std::move(message));
      }
    }

    // Mark the test as skipped, e.g. if the device does not support a
    // feature. No more frames call onFrame().
    void skip(std::string reason) {
      result.skipReason = std::move(reason);
      finished = true;
    }

    // Stop calling onFrame() before TestConfig::frames frames.
    void finish() { finished = true; }

    const TestConfig &getConfig() const { return config; }
    u32 getFrame() const { return frame; }


   private:
    friend class TestWindow;

    const TestConfig &config;
    TestResult &result;
    std::vector<ren::Box<ren::Viewport>> viewports;
    u32 frame = 0;          // The number of frames that called onFrame().
    bool finished = false;  // onFrame() is not called again.
  };



  ////////////////////////////////////////////////////////////////////////////
  // Registration
  ////////////////////////////////////////////////////////////////////////////

  struct TestInfo {
    std::string name;
    std::function<ren::Box<RenderTest>()> make;
    TestConfig config;
    const char *file = "";
  };


  // All tests of the program, sorted by name. The order of static
  // constructors across files is not defined, so the sort makes the order of
  // a run the same each time.
  std::vector<const TestInfo *> getTests();


  // A static instance adds a test to the list. Use REN_RENDER_TEST().
  struct TestRegistration {
    TestRegistration(std::string name, std::function<ren::Box<RenderTest>()> make, TestConfig config, const char *file);
  };


  // Lets REN_RENDER_TEST() take a designated initializer or nothing.
  inline TestConfig makeConfig(TestConfig config = {}) { return config; }

}  // namespace rentest


#define REN_RENDER_TEST_PASTE(x, y) x##y
#define REN_RENDER_TEST_PASTE2(x, y) REN_RENDER_TEST_PASTE(x, y)

// Register a RenderTest subclass under a name. The optional last argument is a
// TestConfig designated initializer, e.g. {.size = {256, 256}}.
#define REN_RENDER_TEST(name, Type, ...)                                            \
  static rentest::TestRegistration REN_RENDER_TEST_PASTE2(g_renderTest_, __LINE__)( \
      name, []() -> ren::Box<rentest::RenderTest> { return ren::makeBox<Type>(); }, rentest::makeConfig(__VA_ARGS__), __FILE__)
