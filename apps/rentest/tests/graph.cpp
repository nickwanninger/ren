// Tests of how the render graph uses resources: updates of color and depth
// attachments, the order of readers and updates, temporal history and its
// reset on resize, image sizes, producers that two viewports share, and a
// compute read of a raster image, and clear values. tests/render_graph.cpp checks the same
// rules on the CPU with fake storage. These tests check that the GPU results
// follow them: that an update loads the old contents, and that the barriers
// and order are correct.

#include "GraphHelpers.h"

#include <ren/renderer/Buffer.h>

using namespace rentest;

namespace {

  // The step that tests/accumulate adds each frame, as bytes. Alpha is 1 from
  // the first frame.
  constexpr u8 kStepRed = 16, kStepGreen = 32;

  // The color after a number of steps.
  glm::u8vec4 accumulatedPixel(u32 steps) { return glm::u8vec4(kStepRed * steps, kStepGreen * steps, 0, 255); }


  // A temporal image that adds a step to its history each frame, and starts
  // from zero when it has no history.
  ren::GraphHandle addAccumulate(ren::GraphScope &graph) {
    // The task reads the history of its own output, so the handle exists only
    // after the task. The bind function reads it through a shared pointer.
    auto previous = ren::make<ren::GraphHandle>();
    const glm::vec4 step(kStepRed / 255.0f, kStepGreen / 255.0f, 0.0f, 1.0f);

    auto &accumulate = graph.addTask<FullscreenTask>("accumulate", "tests/accumulate",
                                                     [previous, step](ren::ShaderCursor &root, const ren::GraphRenderPassContext &ctx) {
                                                       const bool hasHistory = ctx.resources.storage(*previous) != nullptr;
                                                       root.set("history", hasHistory ? ctx.resources.texture(*previous) : ren::TextureHandle{});
                                                       root.set("step", step);
                                                     });

    auto spec = kViewSized;
    spec.temporal = true;
    auto current = accumulate.addColorAttachment("accumulated", spec);
    *previous = graph.graph().previous(current);
    accumulate.read(*previous, ren::GraphAccess::FragmentShaderRead);
    return current;
  }




  ////////////////////////////////////////////////////////////////////////////
  // Updates
  ////////////////////////////////////////////////////////////////////////////

  // One update: a pass fills the image with blue, and a second pass updates the
  // image with a red rectangle. If the update does not load the old contents,
  // the blue is lost.
  class UpdateColor : public rentest::RenderTest {
    void onSetup(rentest::TestContext &ctx) override {
      ctx.makeViewport<GraphViewport>([](ren::GraphScope &graph) {
        auto &base = addFill(graph, "base", kBlue, kEverywhere);
        auto canvas = base.addColorAttachment("canvas", kViewSized);

        auto &update = addFill(graph, "update", kRed, {0.25f, 0.25f, 0.75f, 0.75f});
        return update.updateColorAttachment("canvas", canvas);
      });
    }

    void onFinish(rentest::TestContext &ctx) override {
      ctx.checkPixel("outside the update", {0.1f, 0.1f}, kBluePixel);
      ctx.checkPixel("inside the update", {0.5f, 0.5f}, kRedPixel);
    }
  };


  // Two updates in a chain. The second rectangle overlaps the first, so the
  // overlap shows which update ran last.
  class UpdateChain : public rentest::RenderTest {
    void onSetup(rentest::TestContext &ctx) override {
      ctx.makeViewport<GraphViewport>([](ren::GraphScope &graph) {
        auto &base = addFill(graph, "base", kBlue, kEverywhere);
        auto canvas = base.addColorAttachment("canvas", kViewSized);

        auto &first = addFill(graph, "first update", kRed, {0.1f, 0.1f, 0.6f, 0.6f});
        canvas = first.updateColorAttachment("canvas", canvas);

        auto &second = addFill(graph, "second update", kGreen, {0.4f, 0.4f, 0.9f, 0.9f});
        return second.updateColorAttachment("canvas", canvas);
      });
    }

    void onFinish(rentest::TestContext &ctx) override {
      ctx.checkPixel("base", {0.8f, 0.2f}, kBluePixel);
      ctx.checkPixel("first update only", {0.2f, 0.2f}, kRedPixel);
      ctx.checkPixel("overlap", {0.5f, 0.5f}, kGreenPixel);
      ctx.checkPixel("second update only", {0.8f, 0.8f}, kGreenPixel);
    }
  };


  // A reader of the old version must run before the update, even if it is
  // made after the update. A copy reads the blue base, and an update draws a
  // red rectangle on the same storage. The left half shows the copy, and the
  // right half shows the update. A red left half means that the copy ran after
  // the update, or that the barrier between them is missing.
  class UpdateAfterReader : public rentest::RenderTest {
    void onSetup(rentest::TestContext &ctx) override {
      ctx.makeViewport<GraphViewport>([](ren::GraphScope &graph) {
        auto &base = addFill(graph, "base", kBlue, kEverywhere);
        auto canvas = base.addColorAttachment("canvas", kViewSized);

        // The update is made first, so that the creation order is wrong.
        auto &update = addFill(graph, "update", kRed, kEverywhere);
        auto updated = update.updateColorAttachment("canvas", canvas);

        auto &copy = addSplit(graph, "copy of the old version", canvas, canvas);
        auto snapshot = copy.addColorAttachment("snapshot", kViewSized);

        auto &compose = addSplit(graph, "compose", snapshot, updated);
        return compose.addColorAttachment("composed", kViewSized);
      });
    }

    void onFinish(rentest::TestContext &ctx) override {
      ctx.checkPixel("old version (left)", {0.25f, 0.5f}, kBluePixel);
      ctx.checkPixel("new version (right)", {0.75f, 0.5f}, kRedPixel);
    }
  };


  // An update of a depth attachment. The base pass draws blue at depth 0.5.
  // The update pass loads the color and the depth, and draws a red rectangle
  // in front (depth 0.25) and a green rectangle behind (depth 0.75). If the
  // update does not load the depth, the depth is 1 and the green rectangle
  // shows.
  class UpdateDepth : public rentest::RenderTest {
    void onSetup(rentest::TestContext &ctx) override {
      ctx.makeViewport<GraphViewport>([](ren::GraphScope &graph) {
        auto &base = graph.addTask<DepthFillTask>("base", std::vector<DepthDraw>{{kBlue, kEverywhere, 0.5f}});
        auto color = base.addColorAttachment("color", kViewSized);
        auto depth = base.addDepthAttachment("depth", kViewSized);

        auto &update = graph.addTask<DepthFillTask>("update", std::vector<DepthDraw>{
                                                                  {kRed, {0.1f, 0.25f, 0.45f, 0.75f}, 0.25f},
                                                                  {kGreen, {0.55f, 0.25f, 0.9f, 0.75f}, 0.75f},
                                                              });
        color = update.updateColorAttachment("color", color);
        update.updateDepthAttachment("depth", depth);
        return color;
      });
    }

    void onFinish(rentest::TestContext &ctx) override {
      ctx.checkPixel("base", {0.05f, 0.05f}, kBluePixel);
      ctx.checkPixel("in front of the base", {0.25f, 0.5f}, kRedPixel);
      ctx.checkPixel("behind the base", {0.75f, 0.5f}, kBluePixel);
    }
  };


  // Two color attachments in one pass. The first pass fills the first
  // attachment blue and the second green. A second pass updates only the
  // second attachment with a red square. The left half of the view shows the
  // first attachment, and the right half shows the second. The square must not
  // change the first attachment.
  class MultipleTargets : public rentest::RenderTest {
    void onSetup(rentest::TestContext &ctx) override {
      ctx.makeViewport<GraphViewport>([](ren::GraphScope &graph) {
        auto &base =
            graph.addTask<FullscreenTask>("base", ren::getShader("tests/mrt_fill"), [](ren::ShaderCursor &root, const ren::GraphRenderPassContext &) {
              root.set("color0", kBlue);
              root.set("color1", kGreen);
              root.set("rect", kEverywhere);
            });
        auto first = base.addColorAttachment("first", kViewSized);
        auto second = base.addColorAttachment("second", kViewSized);

        auto &update = addFill(graph, "update second", kRed, {0.25f, 0.25f, 0.75f, 0.75f});
        second = update.updateColorAttachment("second", second);

        auto &compose = addSplit(graph, "compose", first, second);
        return compose.addColorAttachment("composed", kViewSized);
      });
    }

    void onFinish(rentest::TestContext &ctx) override {
      ctx.checkPixel("first attachment", {0.1f, 0.5f}, kBluePixel);
      ctx.checkPixel("first attachment, under the square of the second", {0.4f, 0.5f}, kBluePixel);
      ctx.checkPixel("second attachment, in the square", {0.6f, 0.5f}, kRedPixel);
      ctx.checkPixel("second attachment, outside the square", {0.9f, 0.5f}, kGreenPixel);
    }
  };



  ////////////////////////////////////////////////////////////////////////////
  // History and sizes
  ////////////////////////////////////////////////////////////////////////////

  // A temporal image that adds a step to its history each frame. After N
  // frames, each pixel is N steps. This checks that the history is the result
  // of the last frame, and that the first frame has no history.
  class TemporalAccumulate : public rentest::RenderTest {
   public:
    static constexpr u32 kFrames = 3;

    void onSetup(rentest::TestContext &ctx) override { ctx.makeViewport<GraphViewport>(addAccumulate); }

    void onFinish(rentest::TestContext &ctx) override { ctx.checkPixel("after 3 frames", {0.5f, 0.5f}, accumulatedPixel(kFrames)); }
  };


  // A change of the render size must drop the history. Two frames render at
  // 256x256, then two frames at 128x128. The last image must have 2 steps: 1
  // from the first frame without history, and 1 more. With the old history, it
  // has 4 steps.
  class TemporalResize : public rentest::RenderTest {
   public:
    static constexpr u32 kFrames = 4;
    static constexpr u32 kResizeFrame = 2;

    void onSetup(rentest::TestContext &ctx) override { ctx.makeViewport<GraphViewport>(addAccumulate); }

    void onFrame(rentest::TestContext &ctx, u32 frame) override {
      if (frame == kResizeFrame) {
        ctx.setRenderSize({128, 128});
      }
    }

    void onFinish(rentest::TestContext &ctx) override {
      auto *image = ctx.getFinalImage();
      ctx.check(image && image->width == 128 && image->height == 128, "The final image is not 128x128");
      ctx.checkPixel("2 frames after the resize", {0.5f, 0.5f}, accumulatedPixel(kFrames - kResizeFrame));
    }
  };


  // An image with an absolute size of 4x4 pixels, which does not follow the
  // view. A fill makes it blue, an update makes its top-left quarter red, and
  // a copy stretches it over the view. The checks are at the centers of
  // pixels of the small image, so the filter of the sampler does not matter.
  class AbsoluteSize : public rentest::RenderTest {
    void onSetup(rentest::TestContext &ctx) override {
      ctx.makeViewport<GraphViewport>([](ren::GraphScope &graph) {
        const ren::GraphImageSpec small{.size = ren::ImageSize::absolute({4, 4}), .format = VK_FORMAT_R8G8B8A8_UNORM};

        auto &base = addFill(graph, "base", kBlue, kEverywhere);
        auto image = base.addColorAttachment("small", small);

        auto &update = addFill(graph, "update", kRed, {0.0f, 0.0f, 0.5f, 0.5f});
        image = update.updateColorAttachment("small", image);

        auto &stretch = addSplit(graph, "stretch", image, image);
        return stretch.addColorAttachment("stretched", kViewSized);
      });
    }

    void onFinish(rentest::TestContext &ctx) override {
      ctx.checkPixel("updated pixel", {0.125f, 0.125f}, kRedPixel);
      ctx.checkPixel("base pixel", {0.875f, 0.875f}, kBluePixel);
      ctx.checkPixel("base pixel", {0.875f, 0.125f}, kBluePixel);
    }
  };


  // An image at half the view size. The view is 256x128, which is not square,
  // so the check also finds a swap of width and height. A fill and an update
  // draw into the small image, and a copy stretches it over the view. The
  // copy records the size of the image that it reads.
  class HalfSize : public rentest::RenderTest {
    void onSetup(rentest::TestContext &ctx) override {
      ctx.makeViewport<GraphViewport>([size = this->size](ren::GraphScope &graph) {
        const ren::GraphImageSpec half{.size = ren::ImageSize::viewRelative(0.5f), .format = VK_FORMAT_R8G8B8A8_UNORM};

        auto &base = addFill(graph, "base", kBlue, kEverywhere);
        auto image = base.addColorAttachment("half", half);

        auto &update = addFill(graph, "update", kRed, {0.0f, 0.0f, 0.5f, 0.5f});
        image = update.updateColorAttachment("half", image);

        auto &stretch =
            graph.addTask<FullscreenTask>("stretch", "tests/split", [image, size](ren::ShaderCursor &root, const ren::GraphRenderPassContext &ctx) {
              auto source = ctx.resources.getImage(image);
              *size = {source->getWidth(), source->getHeight()};
              root.set("left", ctx.resources.texture(image));
              root.set("right", ctx.resources.texture(image));
            });
        stretch.read(image, ren::GraphAccess::FragmentShaderRead);
        return stretch.addColorAttachment("stretched", kViewSized);
      });
    }

    void onFinish(rentest::TestContext &ctx) override {
      ctx.check(*size == glm::uvec2(128, 64), fmt::format("The half-size image is {}x{}, not 128x64", size->x, size->y));
      ctx.checkPixel("updated quarter", {0.1f, 0.1f}, kRedPixel);
      ctx.checkPixel("base", {0.9f, 0.1f}, kBluePixel);
      ctx.checkPixel("base", {0.9f, 0.9f}, kBluePixel);
    }

    // Written when the copy records, read in onFinish().
    ren::ref<glm::uvec2> size = ren::make<glm::uvec2>(0, 0);
  };



  ////////////////////////////////////////////////////////////////////////////
  // Sharing
  ////////////////////////////////////////////////////////////////////////////

  // Two viewports, so two GraphScopes with their own views. The first makes
  // an image, and the second reads it. Both scopes enqueue their tasks, but the
  // shared producer must run one time per frame. The window shows the second
  // viewport: the shared image on the left, and its own green image on the
  // right.
  class SharedProducer : public rentest::RenderTest {
   public:
    static constexpr u32 kFrames = 3;

    void onSetup(rentest::TestContext &ctx) override {
      // The viewports build their graphs in the order that they are made, so
      // the producer exists before the consumer needs its handle.
      auto shared = ren::make<ren::GraphHandle>();
      ctx.makeViewport<GraphViewport>([shared, runs = this->runs](ren::GraphScope &graph) {
        auto &producer =
            graph.addTask<FullscreenTask>("shared producer", "tests/fill", [runs](ren::ShaderCursor &root, const ren::GraphRenderPassContext &) {
              (*runs)++;
              root.set("color", kBlue);
              root.set("rect", kEverywhere);
            });
        *shared = producer.addColorAttachment("shared", kViewSized);
        return *shared;
      });

      auto &consumer = ctx.makeViewport<GraphViewport>([shared](ren::GraphScope &graph) {
        auto &own = addFill(graph, "own image", kGreen, kEverywhere);
        auto image = own.addColorAttachment("own", kViewSized);

        auto &compose = addSplit(graph, "compose", *shared, image);
        return compose.addColorAttachment("composed", kViewSized);
      });
      ctx.show(consumer);
    }

    void onFinish(rentest::TestContext &ctx) override {
      ctx.check(*runs == kFrames, fmt::format("The shared producer ran {} times in {} frames", *runs, kFrames));
      ctx.checkPixel("shared image (left)", {0.25f, 0.5f}, kBluePixel);
      ctx.checkPixel("own image (right)", {0.75f, 0.5f}, kGreenPixel);
    }

    // Counted when the producer records, read in onFinish().
    ren::ref<u32> runs = ren::make<u32>(0);
  };



  // A viewport removes its tasks with GraphScope::clear() and builds new ones
  // in the middle of the test. The old graph fills blue, and the new graph
  // fills red with a green square. The old task must stop: it records only in
  // the frames before the rebuild.
  class ScopeRebuild : public rentest::RenderTest {
   public:
    static constexpr u32 kFrames = 4;
    static constexpr u32 kRebuildFrame = 2;

    void onSetup(rentest::TestContext &ctx) override {
      viewport = &ctx.makeViewport<GraphViewport>([oldRuns = this->oldRuns](ren::GraphScope &graph) {
        auto &old = graph.addTask<FullscreenTask>("old fill", "tests/fill", [oldRuns](ren::ShaderCursor &root, const ren::GraphRenderPassContext &) {
          (*oldRuns)++;
          root.set("color", kBlue);
          root.set("rect", kEverywhere);
        });
        return old.addColorAttachment("old", kViewSized);
      });
    }

    void onFrame(rentest::TestContext &ctx, u32 frame) override {
      if (frame != kRebuildFrame) {
        return;
      }

      viewport->rebuild([](ren::GraphScope &graph) {
        auto &base = addFill(graph, "new fill", kRed, kEverywhere);
        auto image = base.addColorAttachment("new", kViewSized);

        auto &update = addFill(graph, "new square", kGreen, {0.25f, 0.25f, 0.75f, 0.75f});
        return update.updateColorAttachment("new", image);
      });
    }

    void onFinish(rentest::TestContext &ctx) override {
      ctx.check(*oldRuns == kRebuildFrame, fmt::format("The old task recorded {} times, not {}", *oldRuns, kRebuildFrame));
      ctx.checkPixel("new fill", {0.1f, 0.1f}, kRedPixel);
      ctx.checkPixel("new square", {0.5f, 0.5f}, kGreenPixel);
    }

    GraphViewport *viewport = nullptr;
    // Counted when the old task records, read in onFinish().
    ren::ref<u32> oldRuns = ren::make<u32>(0);
  };



  ////////////////////////////////////////////////////////////////////////////
  // Clear values
  ////////////////////////////////////////////////////////////////////////////

  // Color clear values from both ways to make an attachment. The left half
  // shows a RenderPassTask attachment that clears to kClearTask and draws a
  // red square. The right half shows a TaskBuilder attachment that clears to
  // kClearBuilder and draws nothing.
  class ClearColor : public rentest::RenderTest {
   public:
    static constexpr glm::vec4 kClearTask{0.25f, 0.5f, 0.75f, 1.0f};
    static constexpr glm::vec4 kClearBuilder{1.0f, 0.5f, 0.0f, 1.0f};

    void onSetup(rentest::TestContext &ctx) override {
      ctx.makeViewport<GraphViewport>([](ren::GraphScope &graph) {
        auto &task = addFill(graph, "task clear", kRed, {0.25f, 0.25f, 0.75f, 0.75f});
        auto left = task.addColorAttachment("task", kViewSized, kClearTask);

        ren::GraphHandle right;
        graph.pass("builder clear").createColorAttachment("builder", kViewSized, right, kClearBuilder).render([](ren::GraphRenderPassContext &) {});

        auto &compose = addSplit(graph, "compose", left, right);
        return compose.addColorAttachment("composed", kViewSized);
      });
    }

    void onFinish(rentest::TestContext &ctx) override {
      ctx.checkPixel("task clear", {0.05f, 0.05f}, glm::u8vec4(glm::round(kClearTask * 255.0f)));
      ctx.checkPixel("task draw", {0.4f, 0.5f}, kRedPixel);
      ctx.checkPixel("builder clear", {0.75f, 0.5f}, glm::u8vec4(glm::round(kClearBuilder * 255.0f)));
    }
  };


  // A depth clear value. The depth clears to 0.5, so a red rectangle at depth
  // 0.25 is visible, and a green rectangle at depth 0.75 is not. With the
  // default clear of 1, the green rectangle shows.
  class ClearDepth : public rentest::RenderTest {
    void onSetup(rentest::TestContext &ctx) override {
      ctx.makeViewport<GraphViewport>([](ren::GraphScope &graph) {
        auto &task = graph.addTask<DepthFillTask>("depth clear", std::vector<DepthDraw>{
                                                                     {kRed, {0.1f, 0.25f, 0.45f, 0.75f}, 0.25f},
                                                                     {kGreen, {0.55f, 0.25f, 0.9f, 0.75f}, 0.75f},
                                                                 });
        auto color = task.addColorAttachment("color", kViewSized, kBlue);
        task.addDepthAttachment("depth", kViewSized, 0.5f);
        return color;
      });
    }

    void onFinish(rentest::TestContext &ctx) override {
      ctx.checkPixel("color clear", {0.05f, 0.05f}, kBluePixel);
      ctx.checkPixel("in front of the depth clear", {0.25f, 0.5f}, kRedPixel);
      ctx.checkPixel("behind the depth clear", {0.75f, 0.5f}, kBluePixel);
    }
  };



  ////////////////////////////////////////////////////////////////////////////
  // Compute
  ////////////////////////////////////////////////////////////////////////////

  // A compute task reads an image that raster passes made: a blue fill, then
  // an update that makes the right half red. The compute task must run after
  // the update, with the image in a shader-read layout. It writes the colors
  // of both halves to a buffer, which the CPU reads after the last frame.
  //
  // The engine cannot write images from compute yet (the bindless heaps have
  // no storage images), so this tests only a compute read.
  class ComputeRead : public rentest::RenderTest {
    void onSetup(rentest::TestContext &ctx) override {
      colors = ren::make<ren::TypedBuffer<glm::vec4>>(2, ren::BufferDomain::Readback, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
      ctx.makeViewport<GraphViewport>([colors = this->colors](ren::GraphScope &graph) {
        auto &base = addFill(graph, "base", kBlue, kEverywhere);
        auto image = base.addColorAttachment("canvas", kViewSized);

        auto &update = addFill(graph, "update", kRed, {0.5f, 0.0f, 1.0f, 1.0f});
        image = update.updateColorAttachment("canvas", image);

        auto program = ren::getShader("tests/compute_read");
        graph.pass("compute read").reads(image, ren::GraphAccess::ComputeShaderRead).execute([program, image, colors](ren::RecordContext &ctx) {
          auto compute = ctx.encoder.bindCompute(program);
          auto args = compute.root("args");
          args.set("image", ctx.resources.texture(image));
          args.set("output", colors->devicePointer<glm::vec4>());
          compute.dispatch({1, 1, 1});
        });
        return image;
      });
    }

    void onFinish(rentest::TestContext &ctx) override {
      const auto *data = colors->hostData();
      for (int i = 0; i < 2; i++) {
        const glm::vec4 expected = i == 0 ? kBlue : kRed;
        const auto &c = data[i];
        ctx.check(c == expected, fmt::format("compute read {}: ({}, {}, {}, {}), but the test expects ({}, {}, {}, {})", i, c.r, c.g, c.b, c.a,
                                             expected.r, expected.g, expected.b, expected.a));
      }
      ctx.checkPixel("left half", {0.25f, 0.5f}, kBluePixel);
      ctx.checkPixel("right half", {0.75f, 0.5f}, kRedPixel);
    }

    // The compute task writes here each frame. The harness waits for the GPU
    // before onFinish(), so the CPU can read it there. Made in onSetup(),
    // because the device does not exist when the test object is made.
    ren::ref<ren::TypedBuffer<glm::vec4>> colors;
  };

}  // namespace


REN_RENDER_TEST("graph.update.multiple-targets", MultipleTargets,
                {.size = {256, 256}, .expect = "Left half blue. Right half green, with a red square that ends at the center line."});

REN_RENDER_TEST("graph.scope.rebuild", ScopeRebuild,
                {.size = {256, 256},
                 .frames = ScopeRebuild::kFrames,
                 .expect = "Red, with a green square in the center. No blue: the first graph is gone."});

REN_RENDER_TEST("graph.clear.color", ClearColor, {.size = {256, 256}, .expect = "Left half steel blue with a red square. Right half orange."});

REN_RENDER_TEST("graph.clear.depth", ClearDepth,
                {.size = {256, 256}, .expect = "Blue, with a red rectangle on the left. No green: it is behind the depth clear."});

REN_RENDER_TEST("graph.compute.read", ComputeRead, {.size = {256, 256}, .expect = "Left half blue, right half red."});

REN_RENDER_TEST("graph.update.color", UpdateColor, {.size = {256, 256}, .expect = "Blue, with a red square in the center."});

REN_RENDER_TEST("graph.update.chain", UpdateChain,
                {.size = {256, 256}, .expect = "Blue. A red square at the top left, and a green square at the bottom right over the red one."});

REN_RENDER_TEST("graph.update.after-reader", UpdateAfterReader,
                {.size = {256, 256}, .expect = "Left half blue (the copy before the update), right half red (the update)."});

REN_RENDER_TEST("graph.temporal.accumulate", TemporalAccumulate,
                {.size = {256, 256}, .frames = TemporalAccumulate::kFrames, .expect = "A flat dark olive color: RGB (48, 96, 0) after 3 frames."});

REN_RENDER_TEST("graph.update.depth", UpdateDepth,
                {.size = {256, 256}, .expect = "Blue, with a red rectangle on the left. No green: it is behind the base."});

REN_RENDER_TEST("graph.temporal.resize", TemporalResize,
                {.size = {256, 256},
                 .frames = TemporalResize::kFrames,
                 .expect = "A flat dark color: RGB (32, 64, 0), 2 frames after the resize to 128x128."});

REN_RENDER_TEST("graph.size.half", HalfSize,
                {.size = {256, 128}, .expect = "A half-size image stretched over the view: blue, with the top-left quarter red."});

REN_RENDER_TEST("graph.shared.producer", SharedProducer,
                {.size = {256, 256}, .frames = SharedProducer::kFrames, .expect = "Left half blue (the shared image), right half green."});

REN_RENDER_TEST("graph.size.absolute", AbsoluteSize,
                {.size = {256, 256}, .expect = "A 4x4 image stretched over the view: blue, with the top-left quarter red."});
