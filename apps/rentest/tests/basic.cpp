#include "RenderTest.h"
#include "ShaderViewport.h"

#include <ren/Camera.h>

#include <glm/gtc/matrix_transform.hpp>

namespace {

  // The smallest test: one fullscreen shader, with no scene.
  class ShaderUV : public rentest::RenderTest {
    void onSetup(rentest::TestContext &ctx) override { ctx.makeViewport<rentest::ShaderViewport>("tests//uv"); }
  };


  // The default ren::Viewport: G-buffer, SSAO and the lighting composite.
  // For now, the only automatic check is that no hook or frame throws.
  class DefaultViewport : public rentest::RenderTest {
    void onSetup(rentest::TestContext &ctx) override {
      auto &viewport = ctx.makeViewport<ren::Viewport>();
      const auto size = glm::vec2(ctx.getConfig().size);
      viewport.view = glm::lookAt(glm::vec3(0.0f, 1.5f, 4.5f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
      viewport.projection = ren::Camera::projectionMatrix(size.x, size.y);
    }
  };

}  // namespace


REN_RENDER_TEST("shader.uv", ShaderUV,
                {.expect = "A UV gradient: black top-left, red top-right, green bottom-left, yellow bottom-right."});

REN_RENDER_TEST("viewport.default", DefaultViewport,
                {.frames = 10, .expect = "The sky and the ground grid of the default viewport, with no errors."});
