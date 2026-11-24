// Tests of the readback of final images that are not RGBA8: float colors
// with each FloatEncoding, and depth.

#include "GraphHelpers.h"

using namespace rentest;

namespace {

  // A fill into an RGBA16F image. The color is outside [0, 1] in some
  // channels, so the test also checks the clamp.
  class FloatFill : public rentest::RenderTest {
   public:
    explicit FloatFill(glm::vec4 color)
        : color(color) {}

    void onSetup(rentest::TestContext &ctx) override {
      ctx.makeViewport<GraphViewport>([color = this->color](ren::GraphScope &graph) {
        const ren::GraphImageSpec spec{.size = ren::ImageSize::viewRelative(1.0f), .format = VK_FORMAT_R16G16B16A16_SFLOAT};
        auto &fill = addFill(graph, "float fill", color, kEverywhere);
        return fill.addColorAttachment("float", spec);
      });
    }

   protected:
    glm::vec4 color;
  };


  // Signed: x * 0.5 + 0.5, the mapping for normals.
  class FloatSigned : public FloatFill {
   public:
    FloatSigned()
        : FloatFill({-1.0f, 0.0f, 0.5f, 1.0f}) {}

    void onFinish(rentest::TestContext &ctx) override { ctx.checkPixel("signed", {0.5f, 0.5f}, {0, 128, 191, 255}); }
  };


  // Srgb: clamp, then the sRGB curve, the mapping for linear colors. Linear
  // 0.25 is sRGB 137, linear 0.5 is sRGB 188, and 2 clamps to 255.
  class FloatSrgb : public FloatFill {
   public:
    FloatSrgb()
        : FloatFill({0.25f, 0.5f, 2.0f, 1.0f}) {}

    void onFinish(rentest::TestContext &ctx) override { ctx.checkPixel("srgb", {0.5f, 0.5f}, {137, 188, 255, 255}); }
  };


  // A depth image as the final image. The left half is at depth 0.25 and the
  // right half at depth 0.75. Readback shows depth as gray.
  class DepthReadback : public rentest::RenderTest {
    void onSetup(rentest::TestContext &ctx) override {
      ctx.makeViewport<GraphViewport>([](ren::GraphScope &graph) {
        auto &fill = graph.addTask<DepthFillTask>("depth fill", std::vector<DepthDraw>{
                                                                    {kBlue, {0.0f, 0.0f, 0.5f, 1.0f}, 0.25f},
                                                                    {kBlue, {0.5f, 0.0f, 1.0f, 1.0f}, 0.75f},
                                                                });
        fill.addColorAttachment("color", kViewSized);
        return fill.addDepthAttachment("depth", kViewSized);
      });
    }

    void onFinish(rentest::TestContext &ctx) override {
      ctx.checkPixel("depth 0.25", {0.25f, 0.5f}, {64, 64, 64, 255});
      ctx.checkPixel("depth 0.75", {0.75f, 0.5f}, {191, 191, 191, 255});
    }
  };

}  // namespace


REN_RENDER_TEST("readback.float.signed", FloatSigned,
                {.size = {256, 256}, .floatEncoding = rentest::FloatEncoding::Signed, .expect = "A flat color: RGB (0, 128, 191)."});

REN_RENDER_TEST("readback.float.srgb", FloatSrgb,
                {.size = {256, 256}, .floatEncoding = rentest::FloatEncoding::Srgb, .expect = "A flat color: RGB (137, 188, 255)."});

REN_RENDER_TEST("readback.depth", DepthReadback, {.size = {256, 256}, .expect = "Left half dark gray (64), right half light gray (191)."});
