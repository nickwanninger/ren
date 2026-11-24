// Tests of the values that each frame gives to the shaders.

#include "RenderTest.h"
#include "ShaderViewport.h"

namespace {

  // The frame globals of a view: time, deltaTime and renderSize. The time step
  // is fixed, so the values of the last frame are exact. The numbers are
  // chosen so that each value is clear in an 8-bit channel: with a time step
  // of 0.125, a frame too many or too few changes the time by 32 steps.
  class FrameGlobals : public rentest::RenderTest {
   public:
    static constexpr u32 kFrames = 3;
    static constexpr float kDeltaTime = 0.125f;

    void onSetup(rentest::TestContext &ctx) override { ctx.makeViewport<rentest::ShaderViewport>("tests/globals"); }

    void onFinish(rentest::TestContext &ctx) override {
      // The first frame has time 0, so the last frame has time (frames - 1) * dt.
      const float time = (kFrames - 1) * kDeltaTime;
      const glm::vec4 expected(time, kDeltaTime, 256.0f / 1024.0f, 128.0f / 1024.0f);
      ctx.checkPixel("globals (time, deltaTime, renderSize / 1024)", {0.5f, 0.5f}, glm::u8vec4(glm::round(expected * 255.0f)));
    }
  };

}  // namespace


REN_RENDER_TEST("frame.globals", FrameGlobals,
                {.size = {256, 128},
                 .frames = FrameGlobals::kFrames,
                 .deltaTime = FrameGlobals::kDeltaTime,
                 .expect = "A flat color. The window shows (64, 32, 64) as RGB; alpha is 32."});
