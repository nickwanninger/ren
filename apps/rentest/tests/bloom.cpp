// Tests of the bloom passes. Bloom keeps only the light above its threshold
// (1.0), so the input is an HDR image: a dark background with a small square
// far above the threshold.

#include "GraphHelpers.h"

#include <ren/renderer/graph/tasks/BloomTask.h>

using namespace rentest;

namespace {

  // The square, in uv, and its value. The background is 0.05, under the
  // threshold, so only the square can make bloom.
  const glm::vec4 kSquare{0.4f, 0.4f, 0.6f, 0.6f};
  const glm::vec4 kBright{8.0f, 8.0f, 8.0f, 1.0f};
  const glm::vec4 kDark{0.05f, 0.05f, 0.05f, 1.0f};


  // Bloom and its tone map. The glow of the square must reach the pixels near
  // it. Without bloom, a pixel near the square has the background color, the
  // same as a far corner.
  class BloomGlow : public rentest::RenderTest {
    void onSetup(rentest::TestContext &ctx) override {
      ctx.makeViewport<GraphViewport>([](ren::GraphScope &graph) {
        const ren::GraphImageSpec hdr{.size = ren::ImageSize::viewRelative(1.0f), .format = VK_FORMAT_R16G16B16A16_SFLOAT};

        auto &background = addFill(graph, "background", kDark, kEverywhere);
        auto scene = background.addColorAttachment("hdr scene", hdr);

        auto &square = addFill(graph, "bright square", kBright, kSquare);
        scene = square.updateColorAttachment("hdr scene", scene);

        ren::GraphHandle output;
        ren::addBloom(graph, scene, output);
        return output;
      });
    }

    void onFinish(rentest::TestContext &ctx) override {
      const auto corner = ctx.pixel({0.05f, 0.05f});
      const auto near = ctx.pixel({0.35f, 0.5f});
      const auto inside = ctx.pixel({0.5f, 0.5f});

      ctx.check(inside.r > 200, fmt::format("The square is {}, not bright", inside.r));
      ctx.check(near.r >= corner.r + 10, fmt::format("No glow near the square: the pixel near it is {}, and the corner is {}", near.r, corner.r));
    }
  };

}  // namespace


REN_RENDER_TEST("bloom.glow", BloomGlow, {.size = {384, 384}, .expect = "A white square in the center of a dark image, with a soft glow around it."});
