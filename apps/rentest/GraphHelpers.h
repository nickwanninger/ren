#pragma once

// Helpers for tests that build their own render graphs: flat colors, fill and
// copy tasks, a depth fill task, and a viewport whose graph is built by a
// function. The images are R8G8B8A8_UNORM and the shaders write flat colors,
// so a test can check exact pixel values, also on a different GPU.

#include "RenderTest.h"

#include <ren/renderer/CommandEncoder.h>
#include <ren/renderer/graph/RenderPassTask.h>
#include <ren/renderer/shader/ShaderCache.h>

namespace rentest {

  inline const glm::vec4 kBlue{0.0f, 0.0f, 1.0f, 1.0f};
  inline const glm::vec4 kRed{1.0f, 0.0f, 0.0f, 1.0f};
  inline const glm::vec4 kGreen{0.0f, 1.0f, 0.0f, 1.0f};

  inline const glm::u8vec4 kBluePixel{0, 0, 255, 255};
  inline const glm::u8vec4 kRedPixel{255, 0, 0, 255};
  inline const glm::u8vec4 kGreenPixel{0, 255, 0, 255};

  // The whole image, as a rectangle for tests/fill.
  inline const glm::vec4 kEverywhere{0.0f, 0.0f, 1.0f, 1.0f};

  inline const ren::GraphImageSpec kViewSized{.size = ren::ImageSize::viewRelative(1.0f), .format = VK_FORMAT_R8G8B8A8_UNORM};



  // A render pass that draws one fullscreen triangle. Each frame, bind() sets
  // the root parameters of the shader.
  class FullscreenTask : public ren::RenderPassTask {
   public:
    using Bind = std::function<void(ren::ShaderCursor &root, const ren::GraphRenderPassContext &ctx)>;

    // Draw a fullscreen surface shader with ren/pass/fullscreen.
    FullscreenTask(ren::RenderGraph &graph, const char *surface, Bind bind)
        : FullscreenTask(graph, ren::getShader(surface, "ren/pass/fullscreen"), std::move(bind)) {
      pso.debugName = surface;
    }

    // Draw a whole program that makes its own fullscreen triangle from
    // SV_VertexID, e.g. tests/mrt_fill.
    FullscreenTask(ren::RenderGraph &graph, ren::ref<ren::ShaderProgram> program, Bind bind)
        : RenderPassTask(graph)
        , bind(std::move(bind)) {
      pso.program = std::move(program);
      pso.hasVertexBinding = false;
      pso.cullMode = ren::CullMode::None;
      pso.depthTest = false;
      pso.depthWrite = false;
    }

    void run(ren::GraphRenderPassContext &ctx) const override {
      auto shader = ctx.encoder.bindGraphics(pso);
      auto root = shader.root("root");
      bind(root, ctx);
      shader.drawFullscreenTriangle();
    }

   private:
    ren::PipelineStateObject pso;
    Bind bind;
  };


  // A fill task: tests/fill with a color and a rectangle.
  inline FullscreenTask &addFill(ren::GraphScope &graph, const char *name, glm::vec4 color, glm::vec4 rect) {
    return graph.addTask<FullscreenTask>(name, "tests/fill", [color, rect](ren::ShaderCursor &root, const ren::GraphRenderPassContext &) {
      root.set("color", color);
      root.set("rect", rect);
    });
  }


  // A split task: tests/split, with left on the left half and right on the
  // right half. Use the same handle for both to copy an image.
  inline FullscreenTask &addSplit(ren::GraphScope &graph, const char *name, ren::GraphHandle left, ren::GraphHandle right) {
    auto &task = graph.addTask<FullscreenTask>(name, "tests/split", [left, right](ren::ShaderCursor &root, const ren::GraphRenderPassContext &ctx) {
      root.set("left", ctx.resources.texture(left));
      root.set("right", ctx.resources.texture(right));
    });
    task.read(left, ren::GraphAccess::FragmentShaderRead);
    task.read(right, ren::GraphAccess::FragmentShaderRead);
    return task;
  }


  // One rectangle of tests/depth_fill.
  struct DepthDraw {
    glm::vec4 color;
    glm::vec4 rect;
    float depth;
  };


  // A render pass that draws rectangles at a depth, with the depth test on.
  // The pipeline compares with LESS, and a new depth attachment is cleared to
  // 1, so a draw is visible where it is nearer than all earlier draws.
  class DepthFillTask : public ren::RenderPassTask {
   public:
    DepthFillTask(ren::RenderGraph &graph, std::vector<DepthDraw> draws)
        : RenderPassTask(graph)
        , draws(std::move(draws)) {
      pso.debugName = "tests/depth_fill";
      pso.program = ren::getShader("tests/depth_fill");
      pso.hasVertexBinding = false;
      pso.cullMode = ren::CullMode::None;
    }

    void run(ren::GraphRenderPassContext &ctx) const override {
      auto shader = ctx.encoder.bindGraphics(pso);
      auto root = shader.root("root");

      // Each set() pushes the constants at once, so each draw gets its own.
      for (auto &draw : draws) {
        root.set("color", draw.color);
        root.set("rect", draw.rect);
        root.set("depth", draw.depth);
        shader.drawFullscreenTriangle();
      }
    }

   private:
    ren::PipelineStateObject pso;
    std::vector<DepthDraw> draws;
  };


  // A viewport whose graph is built by a function. The function returns the
  // handle that the window shows and the harness reads back.
  class GraphViewport : public ren::Viewport {
   public:
    using Build = std::function<ren::GraphHandle(ren::GraphScope &)>;
    explicit GraphViewport(Build build)
        : build(std::move(build)) {}

    // Remove all tasks of this viewport, and build new ones with next. Call it
    // in the update step, e.g. in onFrame(), and only after the first update,
    // which builds the first graph.
    void rebuild(Build next) {
      graph.clear();
      build = std::move(next);
      finalImage = build(graph);
    }

   protected:
    void buildGraph() override { finalImage = build(graph); }

   private:
    Build build;
  };

}  // namespace rentest
