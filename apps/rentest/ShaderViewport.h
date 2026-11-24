#pragma once

#include <ren/renderer/Viewport.h>
#include <ren/renderer/pipelines/PipelineStateObject.h>

#include <string>

namespace rentest {

  // A viewport with one task: it draws a fullscreen surface shader into its
  // output image. The shader exports a Surface that implements
  // IFullscreenSurface, e.g. res/shaders/test.slang.
  class ShaderViewport : public ren::Viewport {
   public:
    explicit ShaderViewport(std::string shader, VkFormat format = VK_FORMAT_R8G8B8A8_UNORM)
        : shader(std::move(shader))
        , format(format) {}

   protected:
    void buildGraph() override;

   private:
    std::string shader;
    VkFormat format;
    ren::PipelineStateObject pso;
  };

}  // namespace rentest
