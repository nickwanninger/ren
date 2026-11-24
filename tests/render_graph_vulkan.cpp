#include <ren/core/Application.h>
#include <ren/core/Flag.h>
#include <ren/renderer/graph/RenderPassTask.h>
#include <iostream>

using namespace ren;
namespace {
  bool failed = false;
  void verify(bool value, const char *message) {
    if (!value) {
      throw std::runtime_error(message);
    }
  }
  void quit() {
    SDL_Event event{};
    event.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&event);
  }
  class TestWindow : public Window {
    ref<ViewContext> a = make<ViewContext>(), b = make<ViewContext>();
    Box<GraphScope> left, right;
    GraphHandle current, previous, other;
    ref<GraphStorage> last, beforeLast;
    std::weak_ptr<Image> resizedImage;
    unsigned tick = 0;
    size_t stableCount = 0;
    ref<Texture> ordinary;
    void onInitialize() override {
      auto &graph = Renderer::get().getGraph();
      left = makeBox<GraphScope>(graph, a);
      right = makeBox<GraphScope>(graph, b);
      left->pass("temporal clear")
          .createColorAttachment("temporal", {.size = ImageSize::viewRelative(0.5f), .format = VK_FORMAT_R8G8B8A8_UNORM, .temporal = true}, current)
          .prepare([this](PrepareContext &) {
            if (tick == 12) {
              throw std::runtime_error("Expected preparation failure");
            }
          })
          .render([](GraphRenderPassContext &) {});
      previous = graph.previous(current);
      right->pass("second view")
          .createColorAttachment("other", {.size = ImageSize::viewRelative(1.0f), .format = VK_FORMAT_R8G8B8A8_UNORM}, other)
          .render([](GraphRenderPassContext &) {});
      ordinary = Texture::createSinglePixel("Ordinary user texture", 255, 0, 255, 255);
    }
    // The frames that fail on purpose. Window::render() discards them.
    bool expectedFailure(const std::string &message) const {
      return (tick == 12 && message == "Expected preparation failure") || (tick == 30 && message == "Expected layout failure") ||
             (tick == 33 && message == "Expected failure after preparation");
    }


    void onRender(FrameContext &frame) override {
      ++tick;
      try {
        exercise(frame);
      } catch (const std::exception &error) {
        if (expectedFailure(error.what())) {
          throw;
        }
        failed = true;
        std::cerr << "Vulkan graph test failed: " << error.what() << '\n';
        quit();
        throw;
      }
    }
    void exercise(FrameContext &frame) {
      auto &renderer = Renderer::get();
      auto &graph = renderer.getGraph();
      const bool resize = tick < 20;
      a->renderSize = resize ? glm::uvec2(tick * 2 + 1, tick + 1) : glm::uvec2(65, 33);
      b->renderSize = {31, 17};
      // Leave this scope unselected, remove it, then reopen it.
      if (tick == 45) {
        right.reset();
      }
      if (tick == 50) {
        right = makeBox<GraphScope>(graph, b);
        right->pass("reopened view")
            .createColorAttachment("reopened", {.size = ImageSize::viewRelative(1.0f), .format = VK_FORMAT_R8G8B8A8_UNORM}, other)
            .render([](GraphRenderPassContext &) {});
      }
      left->enqueue();
      if (right && (tick < 35 || tick >= 50)) {
        right->enqueue();
      }
      ImGui::SetNextWindowSize(ImVec2(300, 220), ImGuiCond_Always);
      ImGui::Begin("Current and previous");
      // Fail with a window open. Window::render() must close it before EndFrame().
      if (tick == 30) {
        throw std::runtime_error("Expected layout failure");
      }
      auto currentRef = renderer.getGraphImages().request(current);
      auto previousRef = renderer.getGraphImages().request(previous);
      ImGui::Image(currentRef, ImVec2(128, 64));
      ImGui::Image(previousRef, ImVec2(128, 64));
      ImGui::Image((ImTextureID)ordinary->getImGui(), ImVec2(16, 16));
      ImGui::End();
      if (right) {
        // Move this window outside the main OS window. ImGui viewports are not
        // enabled, so the window does not detach, and the detached path is not tested.
        if (tick == 25) {
          ImGui::SetNextWindowPos(ImVec2(1100, 100), ImGuiCond_Always);
        }
        ImGui::SetNextWindowSize(ImVec2(260, 160), ImGuiCond_Always);
        ImGui::Begin("Second view and retained output");
        ImGui::Image(renderer.getGraphImages().request(other), ImVec2(128, 64));
        ImGui::End();
      }
      renderer.prepareGraph();
      auto image = std::dynamic_pointer_cast<ImageStorage>(graph.resolve(current));
      // Register in the bindless heap, like a task that samples the image.
      image->textureHandle();
      if (tick == 5) {
        resizedImage = image->image;
      }
      if (!resize && tick > 21) {
        verify(graph.resolve(current) == beforeLast, "Temporal storage is not double buffered");
      }
      verify(image->image->getWidth() == a->renderSize.x / 2 && image->image->getHeight() == std::max(1u, a->renderSize.y / 2),
             "Prepared image size mismatch");
      if (!resize && tick > 20) {
        verify(graph.resolve(previous) == last, "Previous mapping mismatch");
      }
      verify(graph.resolve(previous) != graph.resolve(current), "Temporal current overwrote history");

      // Fail after preparation. The "Previous mapping mismatch" check in the next
      // frame shows that this frame did not save its history.
      if (tick == 33) {
        throw std::runtime_error("Expected failure after preparation");
      }
      // A mutation after capture cannot change prepared output or globals.
      a->renderSize = {1000, 1000};
      renderer.recordGraph();
      verify(currentRef.GetTexID() != ImTextureID_Invalid && previousRef.GetTexID() != ImTextureID_Invalid, "Deferred descriptor is invalid");
      beforeLast = last;
      last = graph.resolve(current);
      auto pass = frame.getMainCommandEncoder()->beginRenderPass(*renderer.getDisplayPass(), getSwapchain().currentTarget());
      drawImGui(pass);
      pass.end();
      if (tick == 40) {
        verify(resizedImage.expired(), "Bindless heap retained a resized image");
      }
      if (tick == 75) {
        stableCount = Image::allImages().size();
      }
      if (tick == 100) {
        verify(Image::allImages().size() <= stableCount + 1, "Image count did not stabilize");
        std::cout << "Vulkan graph tests passed; live images: " << Image::allImages().size() << '\n';
        quit();
      }
    }
  };
}  // namespace
int main(int argc, char **argv) {
  ren::parseFlags(argc, argv);
  try {
    ren::run<TestWindow>("Graph validation", {800, 600});
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return failed ? 1 : 0;
}
