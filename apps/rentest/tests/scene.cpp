// Tests of the engine passes on a real mesh: the default viewport, the
// G-buffer outputs, and SSAO. tests/bloom.cpp tests bloom, which needs an HDR
// input. Each test loads Suzanne at the origin and
// looks at it from a fixed camera. The main check is the golden image; the
// pixel checks only make sure that the mesh is in the image.
//
// ShadowMapTask has no test: its run() is disabled (#if 0) and it has no
// program.

#include "GraphHelpers.h"

#include <ren/Camera.h>
#include <ren/assets/MeshScene.hpp>
#include <ren/core/Components.h>
#include <ren/renderer/graph/tasks/GBufferTask.h>
#include <ren/renderer/graph/tasks/SSAOTask.h>

using namespace rentest;

namespace {

  const char *kMeshPath = "assets/test/meshes/suzanne.glb";

  // The mesh fills the center of the view; the corners show the background.
  const glm::vec2 kCenter{0.5f, 0.5f};
  const glm::vec2 kCorner{0.02f, 0.02f};


  // Load a mesh file into the world of the test, at the origin.
  void loadMesh(const char *path) {
    auto mesh = ren::MeshScene::load(path);
    if (!mesh) {
      throw std::runtime_error(fmt::format("Cannot load {}", path));
    }
    mesh->instantiate(ren::Entity{});
  }


  // Point a viewport at the origin from the front and a little above.
  void placeCamera(ren::Viewport &viewport, glm::uvec2 size) {
    viewport.view = glm::lookAt(glm::vec3(0.0f, 0.5f, 2.5f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    viewport.projection = ren::Camera::projectionMatrix(float(size.x), float(size.y));
  }


  // The outputs of the G-buffer pass, for the build function of a test.
  struct GBuffer {
    ren::GraphHandle albedo, normal, metallicRoughness, depth;
  };

  GBuffer addGBuffer(ren::GraphScope &graph) {
    GBuffer out;
    ren::addGBuffer(graph, out.albedo, out.normal, out.metallicRoughness, out.depth);
    return out;
  }


  // A test that loads the mesh, and shows one image that build() makes from
  // the graph of a GraphViewport.
  class ScenePass : public rentest::RenderTest {
   public:
    explicit ScenePass(GraphViewport::Build build)
        : build(std::move(build)) {}

    void onSetup(rentest::TestContext &ctx) override {
      loadMesh(kMeshPath);
      placeCamera(ctx.makeViewport<GraphViewport>(build), ctx.getConfig().size);
    }

   private:
    GraphViewport::Build build;
  };



  ////////////////////////////////////////////////////////////////////////////
  // Tests
  ////////////////////////////////////////////////////////////////////////////

  // The default ren::Viewport: G-buffer, SSAO, and the lighting composite
  // over the sky.
  class SceneViewport : public rentest::RenderTest {
    void onSetup(rentest::TestContext &ctx) override {
      loadMesh(kMeshPath);
      placeCamera(ctx.makeViewport<ren::Viewport>(), ctx.getConfig().size);
    }

    void onFinish(rentest::TestContext &ctx) override { ctx.check(ctx.pixel(kCenter) != ctx.pixel(kCorner), "The mesh is not in the image"); }
  };


  // Albedo (linear color, sRGB-encoded). The background is cleared to zero.
  class SceneAlbedo : public ScenePass {
   public:
    SceneAlbedo()
        : ScenePass([](ren::GraphScope &graph) { return addGBuffer(graph).albedo; }) {}

    void onFinish(rentest::TestContext &ctx) override {
      ctx.checkPixel("background", kCorner, {0, 0, 0, 0});
      ctx.check(ctx.pixel(kCenter) != ctx.pixel(kCorner), "The mesh is not in the albedo");
    }
  };


  // World-space normals (Signed). The camera looks down -Z, so the front of
  // the mesh has normals toward +Z: blue above the middle value.
  class SceneNormal : public ScenePass {
   public:
    SceneNormal()
        : ScenePass([](ren::GraphScope &graph) { return addGBuffer(graph).normal; }) {}

    void onFinish(rentest::TestContext &ctx) override {
      const auto center = ctx.pixel(kCenter);
      ctx.check(center.b > 160, fmt::format("The normal at the center has blue {}, not a normal toward the camera", center.b));
    }
  };


  // Depth (gray). The background keeps the clear value 1; the mesh is nearer.
  class SceneDepth : public ScenePass {
   public:
    SceneDepth()
        : ScenePass([](ren::GraphScope &graph) { return addGBuffer(graph).depth; }) {}

    void onFinish(rentest::TestContext &ctx) override {
      ctx.checkPixel("background", kCorner, {255, 255, 255, 255});
      ctx.check(ctx.pixel(kCenter).r < 255, "The depth at the center is the clear value; the mesh is not in the depth");
    }
  };


  // SSAO after its blur (Linear). Only the golden image checks it.
  class SceneSSAO : public ScenePass {
   public:
    SceneSSAO()
        : ScenePass([](ren::GraphScope &graph) {
          auto gbuffer = addGBuffer(graph);
          ren::GraphHandle ssao;
          ren::addSSAO(graph, gbuffer.depth, gbuffer.normal, ssao);
          return ssao;
        }) {}
  };

}  // namespace


REN_RENDER_TEST("scene.viewport", SceneViewport, {.size = {384, 384}, .expect = "Suzanne in the center, lit, over the sky and the grid."});

REN_RENDER_TEST("scene.gbuffer.albedo", SceneAlbedo, {.size = {384, 384}, .expect = "Suzanne in her albedo color on black."});

REN_RENDER_TEST("scene.gbuffer.normal", SceneNormal,
                {.size = {384, 384},
                 .floatEncoding = rentest::FloatEncoding::Signed,
                 .expect = "Suzanne in normal colors: mostly blue toward the camera, red to the right, green up."});

REN_RENDER_TEST("scene.gbuffer.depth", SceneDepth, {.size = {384, 384}, .expect = "Suzanne in gray on white."});

REN_RENDER_TEST("scene.ssao", SceneSSAO,
                {.size = {384, 384},
                 .floatEncoding = rentest::FloatEncoding::Linear,
                 .expect = "Ambient occlusion: darker in the folds of Suzanne."});
